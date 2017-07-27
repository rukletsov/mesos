# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# DEFINE DIRECTORY STRUCTURE FOR THIRD-PARTY LIBS.
##################################################

# Convenience variables for include directories of third-party dependencies.
set(PROCESS_INCLUDE_DIR ${MESOS_3RDPARTY_SRC}/libprocess/include)

set(SASL_LFLAG     sasl2)

# Configure the process library, the last of our third-party libraries.
#######################################################################
include(ProcessConfigure)
