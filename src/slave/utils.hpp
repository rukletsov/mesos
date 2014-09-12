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

// Adjust the shutdown timeout (aka shutdown grace period) to be
// shorter than in parents. We make this to give the caller process
// enough time to terminate the underlying process before the caller,
// in turn, is killed by its parent. To adjust the timeout correctly,
// we need the caller to provide its level index in the shutdown chain
// (containerizer has level index 0 and therefore should not adjust
// its timeout). If the default timeout delta cannot be used, we take
// a fraction, though this indicates the timeout is too small to serve
// its purpose.
Duration adjustShutdownTimeout(
    Duration shutdownTimeout,
    int callerLevel);


// Adjust shutdown timeout for ExecutorProcess. We assume it is the
// 1st level (with containerizer being 0) in the shutdown chain.
inline Duration adjustExecutorShutdownTimeout(
    const Duration& baseShutdownTimeout)
{
  return adjustShutdownTimeout(baseShutdownTimeout, 1);
}


// Adjust shutdown timeout for CommandExecutorProcess. We assume it is
// the 2nd level (with containerizer being 0) in the shutdown chain.
inline Duration adjustCommandExecutorShutdownTimeout(
    const Duration& baseShutdownTimeout)
{
  return adjustShutdownTimeout(baseShutdownTimeout, 2);
}

} // namespace slave {
} // namespace internal {
} // namespace mesos {

#endif // __SLAVE_UTILS_HPP__
