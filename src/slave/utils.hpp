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

#ifndef __SLAVE_UTILS_HPP__
#define __SLAVE_UTILS_HPP__

namespace mesos {
namespace internal {
namespace slave {

// Slave           Exec      CommandExecutor
//  +               +               +
//  |               |               |
//  |               |               |
//  |   shutdown()  |               |
//  +-^------------->               |
//  | |             |   shutdown()  |
//  | |             +-^-------------> shutdown()
//  | |             | |             | ^
//  | |             | |             | |
//  | flags.        | timeout       | | timeout
//  | shutdown_     | level 1       | | level 2
//  | grace_period  | |             | v
//  | |             | |             | escalated()
//  | |             | v             |
//  | |             | ShutdownProcess
//  | |             | ::kill()      |
//  | v             |               |
//  | shutdownExecutorTimeout()     |
//  |               |               |
//  v               v               v
//  Containerizer->destroy()


// Returns the shutdown timeout for ExecutorProcess. We assume it is
// the 1st level (with containerizer being 0) in the shutdown chain.
Duration getExecutorShutdownTimeout(const Duration& baseShutdownTimeout);


// Returns the shutdown timeout for CommandExecutorProcess. We assume
// it is the 2nd level (with containerizer being 0) in the shutdown
// chain.
Duration getCommandExecutorShutdownTimeout(const Duration& baseShutdownTimeout);

} // namespace slave {
} // namespace internal {
} // namespace mesos {

#endif // __SLAVE_UTILS_HPP__
