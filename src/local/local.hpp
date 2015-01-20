/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __MESOS_LOCAL_HPP__
#define __MESOS_LOCAL_HPP__

#include <process/process.hpp>

#include "local/flags.hpp"

namespace mesos {
namespace internal {

// Forward declarations.
namespace master {

class Master;

namespace allocation {
class Allocator;
} // namespace allocation {

} // namespace master {

class Configuration;

namespace local {

// Launch a local cluster with the given flags.
process::PID<master::Master> launch(
    const Flags& flags,
    master::allocation::Allocator* _allocator = NULL);

void shutdown();

} // namespace local {
} // namespace internal {
} // namespace mesos {

#endif // __MESOS_LOCAL_HPP__
