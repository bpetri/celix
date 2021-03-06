# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

include_directories(
    ${PROJECT_SOURCE_DIR}/framework/public/include
    ${PROJECT_SOURCE_DIR}/utils/public/include
    ${PROJECT_SOURCE_DIR}/utils/public/include
)


SET(CMAKE_SKIP_BUILD_RPATH  FALSE) #TODO needed?
SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE) #TODO needed?
SET(CMAKE_INSTALL_RPATH "${PROJECT_BINARY_DIR}/framework" "${PROJECT_BINARY_DIR}/utils")

add_executable(test_framework
    run_tests.cpp
    single_framework_test.cpp
    multiple_frameworks_test.cpp
)
target_link_libraries(test_framework celix_framework celix_utils ${CURL_LIBRARIES} ${CPPUTEST_LIBRARY})

configure_file(config.properties.in config.properties @ONLY)
configure_file(framework1.properties.in framework1.properties @ONLY)
configure_file(framework2.properties.in framework2.properties @ONLY)

add_test(NAME run_test_framework COMMAND test_framework)
SETUP_TARGET_FOR_COVERAGE(test_framework_cov test_framework ${CMAKE_BINARY_DIR}/coverage/framework)

