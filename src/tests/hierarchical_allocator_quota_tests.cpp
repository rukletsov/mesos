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

#include <gmock/gmock.h>

namespace mesos {
namespace internal {
namespace tests {

// Those of the overall quota tests that are specific to the built-in
// Hierarchical DRF allocator (i.e. the way quota is satisfied) are in this
// file.

// TODO(alexr): Tests to implement:
//   * Multiple frameworks in a role with set quota (fair allocation).
//   * Quota'ed and non-quota'ed roles, how free pool is allocated.
//   * A framework declines its quota'ed resources (but still gets sufficient
//     offers).
//   * A role may get more resources than its quota in an agent is big
//     enough (granularity).
//   * A role has running tasks, quota is set and is less than the current
//     allocation, some tasks finish or are killed, but the role does not get
//     new non-revocable offers (retroactivity).
//   * Multiple frameworks in a role with set quota, some agents fail,
//     frameworks should be deprived fairly.

} // namespace tests {
} // namespace internal {
} // namespace mesos {
