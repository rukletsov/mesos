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

###############################################################
# This file exports variables needed to link to third-party libs. These are
# used throughout the Mesos project.
#
# This includes things like:
#   * Components defining the public interface, like which headers we need in
#     order to link to libprocess.
#   * Where to look to find built libraries.
#   * Version information of the third-party libraries in use.
#
# This does not include:
#   * Where to find include headers for tests -- the rest of Mesos does not
#     need this information.
#   * Any information about how to build these libraries. That's in
#     libprocess/CMakeLists.txt
#   * Information required to build third-party libraries, wuch as what source
#     files we need to compile libprocess.
#   * Build commands actually used to compile (e.g.) libprocess.
#
# Rationale: Autoconf makes linking to third party dependencies as simple as
# pointing at the underlying makefile. In CMake, this is harder because we do
# not assume there are Makefiles at all. Thus, it is useful to export variables
# with things like which header files you need to include to link to third
# party libraries, and where in the directory tree you need to look to get the
# actual libraries.

if (ENABLE_SSL)
  find_package(OpenSSL REQUIRED)
endif ()

set(PROCESS_PACKAGE_VERSION 0.0.1)
set(PROCESS_PACKAGE_SOVERSION 0)
set(PROCESS_TARGET process-${PROCESS_PACKAGE_VERSION})

# DEFINE PROCESS LIBRARY DEPENDENCIES. Tells the process library build targets
# download/configure/build all third-party libraries before attempting to build.
################################################################################
if (WIN32)
  set(PROCESS_DEPENDENCIES
    ${PROCESS_DEPENDENCIES}
    ${GZIP_TARGET}
    )
endif ()

# DEFINE THIRD-PARTY INCLUDE DIRECTORIES. Tells compiler toolchain where to get
# headers for our third party libs (e.g., -I/path/to/glog on Linux).
###############################################################################
set(PROCESS_INCLUDE_DIRS
  ${PROCESS_INCLUDE_DIRS}
  ${PROCESS_INCLUDE_DIR}
  )

if (ENABLE_SSL)
  set(PROCESS_3RDPARTY_INCLUDE_DIRS
    ${PROCESS_3RDPARTY_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    )
endif ()

# DEFINE THIRD-PARTY LIBS. Used to generate flags that the linker uses to
# include our third-party libs (e.g., -lglog on Linux).
#########################################################################
find_package(Threads REQUIRED)

set(PROCESS_LIBS
  ${PROCESS_LIBS}
  stout
  concurrentqueue
  http_parser
  )

if (NOT ENABLE_LIBEVENT)
  set(PROCESS_LIBS ${PROCESS_LIBS} libev)
else ()
  set(PROCESS_LIBS ${PROCESS_LIBS} libevent)
endif ()

if (ENABLE_SSL)
  set(PROCESS_LIBS
    ${PROCESS_LIBS}
    ${OPENSSL_LIBRARIES}
    )
endif ()
