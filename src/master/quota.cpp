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

#include <string>

#include <mesos/mesos.hpp>
#include <mesos/resources.hpp>

#include <stout/error.hpp>
#include <stout/option.hpp>

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
  // If there is already quota stored for the role, find the index of
  // the corresponding entry.
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
      registry->mutable_quotas()->DeleteSubrange(i, 1);
      changed = true; // Mutation

      // NOTE: Multiple entries per role are not allowed.
      break;
    }
  }

  return changed;
}


namespace validation {

using mesos::quota::QuotaInfo;
using std::string;

Try<Nothing> quotaInfo(const QuotaInfo& quotaInfo)
{
  // The reference role for the quota request.
  string role = quotaInfo.role();

  foreach (const Resource& resource, quotaInfo.guarantee()) {
    // Check that each resource is valid.
    Option<Error> error = Resources::validate(resource);
    if (error.isSome()) {
      return Error(
          "Quota request with invalid resource: " + error.get().message);
    }

    // Check that `Resource` does not contain non-relevant fields for quota.

    if (resource.has_reservation()) {
      return Error("Quota request may not contain ReservationInfo");
    }
    if (resource.has_disk()) {
      return Error("Quota request may not contain DiskInfo");
    }
    if (resource.has_revocable()) {
      return Error("Quota request may not contain RevocableInfo");
    }

    // Check that the `Resource` is scalar.
    if (resource.type() != Value::SCALAR) {
      return Error(
          "Quota request may not include non-scalar resources");
    }

    // Check all roles are set and equal.

    if (!resource.has_role()) {
      return Error("Quota request without role specified");
    }
    if (resource.role().empty()) {
      return Error("Quota request with empty role specified");
    }
    if (role.empty()) {
      // Store first encountered role as reference.
      role = resource.role();
    } else if (role != resource.role() ) {
      // All roles should be equal across a quota request.
      return Error("Quota request with different roles: '" + role +
                   "','" + resource.role() + "'");
    }
  }

  return Nothing();
}

} // namespace validation {

} // namespace quota {
} // namespace master {
} // namespace internal {
} // namespace mesos {
