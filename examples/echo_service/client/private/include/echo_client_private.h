/**
 *Licensed to the Apache Software Foundation (ASF) under one
 *or more contributor license agreements.  See the NOTICE file
 *distributed with this work for additional information
 *regarding copyright ownership.  The ASF licenses this file
 *to you under the Apache License, Version 2.0 (the
 *"License"); you may not use this file except in compliance
 *with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing,
 *software distributed under the License is distributed on an
 *"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 *specific language governing permissions and limitations
 *under the License.
 */
/*
 * echo_client_private.h
 *
 *  \date       Sep 21, 2010
 *  \author    	<a href="mailto:dev@celix.apache.org">Apache Celix Project Team</a>
 *  \copyright	Apache License, Version 2.0
 */

#ifndef ECHO_CLIENT_PRIVATE_H_
#define ECHO_CLIENT_PRIVATE_H_

#include <celixbool.h>

struct echoClient {
	service_tracker_pt tracker;
	bool running;
	pthread_t sender_thread;
	char *ident;
};

typedef struct echoClient * echo_client_pt;

echo_client_pt echoClient_create(service_tracker_pt context);

void echoClient_start(echo_client_pt client);
void echoClient_stop(echo_client_pt client);

void echoClient_destroy(echo_client_pt client);


#endif /* ECHO_CLIENT_PRIVATE_H_ */
