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

#include "master/quota.hpp"

namespace mesos {
namespace internal {
namespace master {
namespace quota {

UpdateQuota::UpdateQuota(const mesos::quota::QuotaInfo& quotaInfo)
  : info(quotaInfo) {}


Try<bool> UpdateQuota::perform(
    Registry* registry,
    hashset<SlaveID>*,
    bool)
{
  // If there is already quota stored for the role, find the index of the
  // corresponding entry.
  int existingRoleIndex = -1;
  for (int i = 0; i < registry->quotas().size(); ++i) {
    const Registry::Quota& quota = registry->quotas(i);

    if (quota.info().role() == info.role()) {
      existingRoleIndex = i;
      break;
    }
  }

  // Update an entry or create a new one if there is none.
  Registry::Quota* quota = (existingRoleIndex >= 0)
    ? registry->mutable_quotas()->Mutable(existingRoleIndex)
    : registry->mutable_quotas()->Add();

  quota->mutable_info()->CopyFrom(info);

  return true; // Mutation.
}


RemoveQuota::RemoveQuota(const std::string& _role) : role(_role) {}


Try<bool> RemoveQuota::perform(
    Registry* registry,
    hashset<SlaveID>*,
    bool)
{
  bool changed = false;

  // Remove quota for the role if a corresponding entry exists.
  for (int i = 0; i < registry->quotas().size(); ++i) {
    const Registry::Quota& quota = registry->quotas(i);

    if (quota.info().role() == role) {
      // NOTE: `DeleteSubrange()` removes an element from the array and shifts
      // elements after it down. This should be fine, since we do not expect a
      // lot of churn quota requests in the cluster.
      registry->mutable_quotas()->DeleteSubrange(i, 1);
      changed = true; // Mutation

      // NOTE: Multiple entries per role are not allowed.
      break;
    }
  }

  return changed;
}

} // namespace quota {
} // namespace master {
} // namespace internal {
} // namespace mesos {
