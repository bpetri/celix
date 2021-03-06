/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * endpoint_discovery_poller.c
 *
 * \date       3 Jul 2014
 * \author     <a href="mailto:dev@celix.apache.org">Apache Celix Project Team</a>
 * \copyright  Apache License, Version 2.0
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>

#include "bundle_context.h"
#include "log_helper.h"
#include "utils.h"

#include "discovery_impl.h"

#include "endpoint_descriptor_reader.h"


#define DISCOVERY_POLL_INTERVAL "DISCOVERY_CFG_POLL_INTERVAL"
#define DEFAULT_POLL_INTERVAL "10"


static void *endpointDiscoveryPoller_performPeriodicPoll(void *data);
celix_status_t endpointDiscoveryPoller_poll(endpoint_discovery_poller_pt poller, char *url, array_list_pt currentEndpoints);
static celix_status_t endpointDiscoveryPoller_getEndpoints(endpoint_discovery_poller_pt poller, char *url, array_list_pt *updatedEndpoints);
static celix_status_t endpointDiscoveryPoller_endpointDescriptionEquals(void *endpointPtr, void *comparePtr, bool *equals);

/**
 * Allocates memory and initializes a new endpoint_discovery_poller instance.
 */
celix_status_t endpointDiscoveryPoller_create(discovery_pt discovery, bundle_context_pt context, endpoint_discovery_poller_pt *poller) {
    celix_status_t status;

    *poller = malloc(sizeof(struct endpoint_discovery_poller));
    if (!*poller) {
        return CELIX_ENOMEM;
    }

    (*poller)->loghelper = &discovery->loghelper;

	status = celixThreadMutex_create(&(*poller)->pollerLock, NULL);
	if (status != CELIX_SUCCESS) {
		return status;
	}

	char* interval = NULL;
	status = bundleContext_getProperty(context, DISCOVERY_POLL_INTERVAL, &interval);
	if (!interval) {
		interval = DEFAULT_POLL_INTERVAL;
	}

	char* endpoints = NULL;
	status = bundleContext_getProperty(context, DISCOVERY_POLL_ENDPOINTS, &endpoints);
	if (!endpoints) {
		endpoints = DEFAULT_POLL_ENDPOINTS;
	}
	// we're going to mutate the string with strtok, so create a copy...
	endpoints = strdup(endpoints);

	(*poller)->poll_interval = atoi(interval);
	(*poller)->discovery = discovery;
	(*poller)->running = false;
	(*poller)->entries = hashMap_create(utils_stringHash, NULL, utils_stringEquals, NULL);

	const char* sep = ",";
	char *save_ptr = NULL;
	char* tok = strtok_r(endpoints, sep, &save_ptr);
	while (tok) {
		endpointDiscoveryPoller_addDiscoveryEndpoint(*poller, utils_stringTrim(tok));
		tok = strtok_r(NULL, sep, &save_ptr);
	}
	// Clean up after ourselves...
	free(endpoints);

    status = celixThreadMutex_lock(&(*poller)->pollerLock);
    if (status != CELIX_SUCCESS) {
        return CELIX_BUNDLE_EXCEPTION;
    }

	(*poller)->running = true;

	status = celixThread_create(&(*poller)->pollerThread, NULL, endpointDiscoveryPoller_performPeriodicPoll, *poller);
	if (status != CELIX_SUCCESS) {
		return status;
	}


    status = celixThreadMutex_unlock(&(*poller)->pollerLock);

    return status;
}

/**
 * Destroys and frees up memory for a given endpoint_discovery_poller struct.
 */
celix_status_t endpointDiscoveryPoller_destroy(endpoint_discovery_poller_pt poller) {
    celix_status_t status;

    poller->running = false;

    celixThread_join(poller->pollerThread, NULL);

    hash_map_iterator_pt iterator = hashMapIterator_create(poller->entries);
	while (hashMapIterator_hasNext(iterator)) {
		hash_map_entry_pt entry = hashMapIterator_nextEntry(iterator);

		if ( endpointDiscoveryPoller_removeDiscoveryEndpoint(poller, (char*) hashMapEntry_getKey(entry)) == CELIX_SUCCESS) {
			hashMapIterator_destroy(iterator);
			iterator = hashMapIterator_create(poller->entries);
		}
	}
	hashMapIterator_destroy(iterator);

	status = celixThreadMutex_lock(&poller->pollerLock);

	if (status != CELIX_SUCCESS) {
		return CELIX_BUNDLE_EXCEPTION;
	}

	hashMap_destroy(poller->entries, true, false);

    status = celixThreadMutex_unlock(&poller->pollerLock);

    poller->loghelper = NULL;

    free(poller);

	return status;
}


celix_status_t endpointDiscoveryPoller_getDiscoveryEndpoints(endpoint_discovery_poller_pt poller, array_list_pt urls) {
    celixThreadMutex_lock(&(poller)->pollerLock);

    hash_map_iterator_pt iterator = hashMapIterator_create(poller->entries);

    while(hashMapIterator_hasNext(iterator))  {
        hash_map_entry_pt entry = hashMapIterator_nextEntry(iterator);
        char* toAdd = strdup((char*) hashMapEntry_getKey(entry));
        arrayList_add(urls, toAdd);
    }

    hashMapIterator_destroy(iterator);

    celixThreadMutex_unlock(&(poller)->pollerLock);

    return CELIX_SUCCESS;
}

/**
 * Adds a new endpoint URL to the list of polled endpoints.
 */
celix_status_t endpointDiscoveryPoller_addDiscoveryEndpoint(endpoint_discovery_poller_pt poller, char *url) {
	celix_status_t status;

	status = celixThreadMutex_lock(&(poller)->pollerLock);
	if (status != CELIX_SUCCESS) {
		return CELIX_BUNDLE_EXCEPTION;
	}

	// Avoid memory leaks when adding an already existing URL...
	array_list_pt endpoints = hashMap_get(poller->entries, url);
	if (endpoints == NULL) {
		status = arrayList_createWithEquals(endpointDiscoveryPoller_endpointDescriptionEquals, &endpoints);

		if (status == CELIX_SUCCESS) {
			logHelper_log(*poller->loghelper, OSGI_LOGSERVICE_DEBUG, "ENDPOINT_POLLER: add new discovery endpoint with url %s", url);
			hashMap_put(poller->entries, strdup(url), endpoints);
			endpointDiscoveryPoller_poll(poller, url, endpoints);
		}
	}

	status = celixThreadMutex_unlock(&poller->pollerLock);

	return status;
}

/**
 * Removes an endpoint URL from the list of polled endpoints.
 */
celix_status_t endpointDiscoveryPoller_removeDiscoveryEndpoint(endpoint_discovery_poller_pt poller, char *url) {
	celix_status_t status = CELIX_SUCCESS;

	if (celixThreadMutex_lock(&poller->pollerLock) != CELIX_SUCCESS) {
		status = CELIX_BUNDLE_EXCEPTION;
	} else {
		hash_map_entry_pt entry = hashMap_getEntry(poller->entries, url);

		if (entry == NULL) {
			logHelper_log(*poller->loghelper, OSGI_LOGSERVICE_DEBUG, "ENDPOINT_POLLER: There was no entry found belonging to url %s - maybe already removed?", url);
		} else {
			char* origKey = hashMapEntry_getKey(entry);

			logHelper_log(*poller->loghelper, OSGI_LOGSERVICE_DEBUG, "ENDPOINT_POLLER: remove discovery endpoint with url %s", url);

			array_list_pt entries = hashMap_remove(poller->entries, url);

			for (unsigned int i = arrayList_size(entries); i > 0; i--) {
				endpoint_description_pt endpoint = arrayList_get(entries, i - 1);
				discovery_removeDiscoveredEndpoint(poller->discovery, endpoint);
				arrayList_remove(entries, i - 1);
				endpointDescription_destroy(endpoint);
			}

			if (entries != NULL) {
				arrayList_destroy(entries);
			}

			free(origKey);
		}
		status = celixThreadMutex_unlock(&poller->pollerLock);
	}

	return status;
}




celix_status_t endpointDiscoveryPoller_poll(endpoint_discovery_poller_pt poller, char *url, array_list_pt currentEndpoints) {
	celix_status_t status;
	array_list_pt updatedEndpoints = NULL;

	// create an arraylist with a custom equality test to ensure we can find endpoints properly...
	arrayList_createWithEquals(endpointDiscoveryPoller_endpointDescriptionEquals, &updatedEndpoints);
	status = endpointDiscoveryPoller_getEndpoints(poller, url, &updatedEndpoints);

	if (status != CELIX_SUCCESS) {
		status = celixThreadMutex_unlock(&poller->pollerLock);
	} else {
		if (updatedEndpoints) {
			for (unsigned int i = arrayList_size(currentEndpoints); i > 0; i--) {
				endpoint_description_pt endpoint = arrayList_get(currentEndpoints, i - 1);

				if (!arrayList_contains(updatedEndpoints, endpoint)) {
					status = discovery_removeDiscoveredEndpoint(poller->discovery, endpoint);
					arrayList_remove(currentEndpoints, i - 1);
					endpointDescription_destroy(endpoint);
				}
			}

			for (int i = arrayList_size(updatedEndpoints); i > 0; i--) {
				endpoint_description_pt endpoint = arrayList_remove(updatedEndpoints, 0);

				if (!arrayList_contains(currentEndpoints, endpoint)) {
					arrayList_add(currentEndpoints, endpoint);
					status = discovery_addDiscoveredEndpoint(poller->discovery, endpoint);
				} else {
					endpointDescription_destroy(endpoint);

				}
			}
		}

		if (updatedEndpoints) {
			arrayList_destroy(updatedEndpoints);
		}
	}

	return status;
}

static void *endpointDiscoveryPoller_performPeriodicPoll(void *data) {
	endpoint_discovery_poller_pt poller = (endpoint_discovery_poller_pt) data;

	useconds_t interval = (useconds_t) (poller->poll_interval * 1000000L);

	while (poller->running) {
		usleep(interval);
		celix_status_t status = celixThreadMutex_lock(&poller->pollerLock);

		if (status != CELIX_SUCCESS) {
			logHelper_log(*poller->loghelper, OSGI_LOGSERVICE_WARNING, "ENDPOINT_POLLER: failed to obtain lock; retrying...");
		} else {
			hash_map_iterator_pt iterator = hashMapIterator_create(poller->entries);

			while (hashMapIterator_hasNext(iterator)) {
				hash_map_entry_pt entry = hashMapIterator_nextEntry(iterator);

				char *url = hashMapEntry_getKey(entry);
				array_list_pt currentEndpoints = hashMapEntry_getValue(entry);

				endpointDiscoveryPoller_poll(poller, url, currentEndpoints);
			}

			hashMapIterator_destroy(iterator);
		}

		status = celixThreadMutex_unlock(&poller->pollerLock);
		if (status != CELIX_SUCCESS) {
			logHelper_log(*poller->loghelper, OSGI_LOGSERVICE_WARNING, "ENDPOINT_POLLER: failed to release lock; retrying...");
		}
	}

	return NULL;
}



struct MemoryStruct {
  char *memory;
  size_t size;
};

static size_t endpointDiscoveryPoller_writeMemory(void *contents, size_t size, size_t nmemb, void *memoryPtr) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)memoryPtr;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if(mem->memory == NULL) {
    	printf("ENDPOINT_POLLER: not enough memory (realloc returned NULL)!");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

static celix_status_t endpointDiscoveryPoller_getEndpoints(endpoint_discovery_poller_pt poller, char *url, array_list_pt *updatedEndpoints) {
    celix_status_t status = CELIX_SUCCESS;

	logHelper_log(*poller->loghelper, OSGI_LOGSERVICE_DEBUG, "Polling url '%s'", url);

    CURL *curl = NULL;
    CURLcode res = CURLE_OK;

    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl = curl_easy_init();
    if (!curl) {
        status = CELIX_ILLEGAL_STATE;
    } else {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, endpointDiscoveryPoller_writeMemory);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        res = curl_easy_perform(curl);

        curl_easy_cleanup(curl);
    }

    // process endpoints file
    if (res == CURLE_OK) {
        endpoint_descriptor_reader_pt reader = NULL;

    	status = endpointDescriptorReader_create(poller, &reader);
    	if (status == CELIX_SUCCESS) {
			logHelper_log(*poller->loghelper, OSGI_LOGSERVICE_DEBUG, "Polled data '%s'", chunk.memory);
			status = endpointDescriptorReader_parseDocument(reader, chunk.memory, updatedEndpoints);
    	}

    	if (reader) {
			endpointDescriptorReader_destroy(reader);
    	}
    } else {
    	logHelper_log(*poller->loghelper, OSGI_LOGSERVICE_ERROR, "ENDPOINT_POLLER: unable to read endpoints from %s, reason: %s", url, curl_easy_strerror(res));
    }

    // clean up endpoints file
    if (chunk.memory) {
        free(chunk.memory);
    }

    return status;
}

static celix_status_t endpointDiscoveryPoller_endpointDescriptionEquals(void *endpointPtr, void *comparePtr, bool *equals) {
    endpoint_description_pt endpoint = (endpoint_description_pt) endpointPtr;
    endpoint_description_pt compare = (endpoint_description_pt) comparePtr;

    if (strcmp(endpoint->id, compare->id) == 0) {
        *equals = true;
    } else {
        *equals = false;
    }

    return CELIX_SUCCESS;
}
