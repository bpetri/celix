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

#include "celix/dm/DependencyManager.h"
#include <typeinfo>

using namespace celix::dm;

DependencyManager::DependencyManager(bundle_context_pt context) : context(context), components() {
    this->cDepMan = nullptr;
    dependencyManager_create(context, &this->cDepMan);
}

DependencyManager::~DependencyManager() {
    dependencyManager_destroy(this->cDepMan);
    this->cDepMan = nullptr;
}

const bundle_context_pt DependencyManager::bundleContext() const {
    return context;
}

const dm_dependency_manager_pt DependencyManager::cDependencyManager() const {
    return cDepMan;
}

void DependencyManager::start() {
    for(BaseComponent* cmp : addedComponents)  {
        dependencyManager_add(cDepMan, cmp->cComponent());
    }
}

void DependencyManager::stop() {
    dependencyManager_removeAllComponents(cDepMan);
    components.clear();
    addedComponents.clear();
    dependencies.clear();
}



