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
* limitations under the License
*/

#include "master/master.hpp"

#include <mesos/resources.hpp>

#include <mesos/quota/quota.hpp>

#include <process/defer.hpp>
#include <process/future.hpp>
#include <process/http.hpp>
#include <process/owned.hpp>

#include <stout/protobuf.hpp>

#include "logging/logging.hpp"

#include "master/quota.hpp"
#include "master/registrar.hpp"

namespace http = process::http;

using std::string;

using http::Accepted;
using http::BadRequest;
using http::Conflict;
using http::OK;

using process::Future;
using process::Owned;

using mesos::quota::QuotaInfo;

namespace mesos {
namespace internal {
namespace master {

Try<QuotaInfo> Master::QuotaHandler::extractQuotaInfo(
    const JSON::Array& requestJSON) const
{
  VLOG(1) << "Constructing QuotaInfo from quota request";

  // Create `QuotaInfo` Protobuf.
  Try<google::protobuf::RepeatedPtrField<Resource>> resources =
    ::protobuf::parse<google::protobuf::RepeatedPtrField<Resource>>(
        requestJSON);

  if (resources.isError()) {
    return Error(
        "Error in parsing 'resources' in quota request: " + resources.error());
  }

  // Check that the request contains at least one resource.
  if (resources.get().size() == 0) {
    return Error("Quota request with empty 'resources'");
  }

  // Validate reference role.
  if (!resources.get().Get(0).has_role()) {
    return Error("Quota request without role specified");
  }

  if (resources.get().Get(0).role().empty()) {
    return Error("Quota request with empty role specified");
  }

  // Get role of first resource.
  string role = resources.get().Get(0).role();


  QuotaInfo quota;
  quota.mutable_guarantee()->CopyFrom(resources.get());
  quota.set_role(role);

  return quota;
}


Option<Error> Master::QuotaHandler::checkSanity(const QuotaInfo& request) const
{
  VLOG(1) << "Checking sanity of a set quota request";

  // This should have been validated earlier.
  CHECK(master->roles.contains(request.role()));

  // Calculate current resource allocation per role (including used
  // resources, outstanding offers, but not static reservations).
  //
  // TODO(alexr): Consider counting dynamic reservation towards quota.
  // Currently they are not included in allocated or used resources
  // (see MESOS-3338) and there is no way to get the total amout of
  // dynamically reserved resources for a role without looping through
  // all agents.
  Resources roleTotal = master->roles[request.role()]->resources();

  // Calculate the unsatisfied part of role quota. It can be satisfied
  // only from '*' resources, hence we remove the role via `flatten()`.
  Resources missingResources =
    Resources(request.guarantee()).flatten() - roleTotal.flatten();

  // Estimate total resources available in the cluster.
  Resources availableInCluster;
  foreachvalue (Slave* slave, master->slaves.registered) {
    if (!slave->connected || !slave->active) {
      continue;
    }

    Resources availableOnAgent =
      slave->totalResources -
      Resources::sum(slave->usedResources);

    // We use '*' and dynamically reserved resourcesto satisfy quota,
    // but we exclude statically reserved resources.
    //
    // TODO(alexr): Once dynamically reserved resources are included in
    // `roleTotal`, we should remove them from here to avoid double accounting.
    Resources availableForRole =
      availableOnAgent.unreserved() +
      availableOnAgent.reserved(
          request.role()).filter(Resources::isDynamicallyReserved);

    // We flatten resources because we are not interested in reservation
    // details.
    availableInCluster += availableForRole.flatten();

    // If we have found enough resources there is no need to continue.
    if (availableInCluster.contains(missingResources)) {
      return None();
    }
  }

  // If we reached this point, there are not enough available resources
  // in the cluster, hence the request cannot be satisfied.
  return Error("Not enough available resources to satisfy quota request");
}


Future<http::Response> Master::QuotaHandler::set(
    const http::Request& request) const
{
  // Authenticate and authorize the request.
  // TODO(alexr): Check Master::Http::authenticate() for an example.

  // Check that the request type is POST which is guaranteed by the master.
  CHECK_EQ(request.method, "POST");

  // Validate request and extract JSON.

  Try<hashmap<string, string>> decode = http::query::decode(request.body);
  if (decode.isError()) {
    return BadRequest("Unable to decode query string: " + decode.error());
  }

  const hashmap<string, string>& values = decode.get();

  if (values.get("resources").isNone()) {
    return BadRequest("Missing 'resources' query parameter");
  }

  Try<JSON::Array> parse = JSON::parse<JSON::Array>(
      values.get("resources").get());

  if (parse.isError()) {
    return BadRequest("Failed to parse JSON: " + parse.error());
  }

  // Convert the request JSON to `QuotaInfo` protobuf message.
  Try<QuotaInfo> extract = extractQuotaInfo(parse.get());
  if (extract.isError()) {
    return BadRequest("Failed to convert request into QuotaInfo: " +
                      extract.error());
  }

  // Check that the `QuotaInfo` is a valid quota request.
  Try<Nothing> validate = quota::validation::quotaInfo(extract.get());
  if (validate.isError()) {
    return BadRequest("Failed to validate set quota request: " +
                      validate.error());
  }

  // Check that the role is known by the master.
  // TODO(alexr): Once we are able to dynamically add roles, we should stop
  // checking whether the requested role is known to the master, because an
  // operator may set quota for a role that is about to be introduced.
  if (!master->roles.contains(extract.get().role())) {
    return BadRequest("Quota request for unknown role: '" +
                      extract.get().role() + "'");
  }

  // Check we are not updating an existing quota.
  // TODO(joerg84): Update error message once quota update is in place.
  if (master->quotas.contains(extract.get().role())) {
    return BadRequest(
        "Quota cannot be set for a role that already has quota");
  }

  const QuotaInfo& quotaInfo = extract.get();

  // Validate whether a quota request can be satisfied.
  Option<Error> error = checkSanity(quotaInfo);
  if (error.isSome()) {
    return Conflict("Sanity check for set quota request failed: " +
                    error.get().message);
  }

  // Populate master's quota-related local state. We do this before updating
  // the registry in order to make sure that we are not already trying to
  // satisfy a request for this role (since this is a multi-phase event).
  // NOTE: We do not need to remove quota for the role if the registry update
  // fails because in this case the master fails as well.
  Quota quota = {quotaInfo};
  master->quotas[quotaInfo.role()] = quota;

  // Update the registry with the new quota.
  // TODO(alexr): MESOS-3165.

  // Update registry with the new quota and grant the request.
  return master->registrar->apply(Owned<Operation>(
      new quota::UpdateQuota(quotaInfo)))
    .then(defer(master->self(), [=](bool result) -> Future<http::Response> {
      // See the top comment in "master/quota.hpp" for why this check is here.
      CHECK(result);

      master->allocator->setQuota(quotaInfo.role(), quotaInfo);

      return OK();
    }));
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
