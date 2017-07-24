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

# DEFINE THIRD-PARTY LIBS. Used to generate flags that the linker uses to
# include our third-party libs (e.g., -lglog on Linux).
#########################################################################
set(STOUT_LIBS
  ${STOUT_LIBS}
  ${CMAKE_THREAD_LIBS_INIT}
  apr
  boost
  curl
  elfio
  glog
  picojson
  protobuf
  zlib
  )

if (WIN32)
  set(STOUT_LIBS
    ${STOUT_LIBS}
    ws2_32
    Mswsock
    Secur32
    Userenv
    )
else ()
  set(STOUT_LIBS
    ${STOUT_LIBS}
    svn
    ${DL_LFLAG}
    )
endif ()

# TODO(hausdorff): The `LINUX` flag comes from MesosConfigure; when we
# port the bootstrap script to CMake, we should also copy this logic
# into .cmake files in the Stout and Process libraries' folders
# individually.
if (LINUX)
  set(STOUT_LIBS ${STOUT_LIBS} rt)
endif ()
