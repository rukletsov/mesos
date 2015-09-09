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

#include <mesos/resources.hpp>

#include <mesos/quota/quota.hpp>

#include <process/defer.hpp>
#include <process/future.hpp>
#include <process/http.hpp>

#include <stout/protobuf.hpp>

#include "logging/logging.hpp"

#include "master/master.hpp"

namespace http = process::http;

using std::string;

using http::Accepted;
using http::BadRequest;
using http::Conflict;
using http::OK;

using process::Future;

using mesos::quota::QuotaInfo;

namespace mesos {
namespace internal {
namespace master {

Try<Nothing> Master::QuotaHandler::validateRequest(
    const http::Request& request) const
{
  VLOG(1) << "Validating quota request: '" << request.body << "'";

  // Parse the query string in the request body.
  Try<hashmap<string, string>> decode = http::query::decode(request.body);

  if (decode.isError()) {
    return Error("Unable to decode query string: " + decode.error());
  }

  hashmap<string, string> values = decode.get();

  if (values.get("resources").isNone()) {
    return Error("Missing 'resources' query parameter");
  }

  Try<JSON::Array> parse = JSON::parse<JSON::Array>(values["resources"]);
  if (parse.isError()) {
    return Error("Failed to parse JSON: " + parse.error());
  }

  return Nothing();
}

Try<QuotaInfo> Master::QuotaHandler::extractQuotaInfo(
    const http::Request& request) const
{
  VLOG(1) << "Extracting QuotaInfo from quota request: '"
          << request.body << "'";

  // NOTE: Request has been checked before in `validateRequest()`.
  Try<hashmap<string, string>> decode =
    http::query::decode(request.body);

  if (decode.isError()) {
    return Error("Unable to decode query string: " + decode.error());
  }

  hashmap<string, string> values = decode.get();

  if (values.get("resources").isNone()) {
    return Error("Missing 'resources' query parameter");
  }

  Try<JSON::Array> parse = JSON::parse<JSON::Array>(values["resources"]);
  if (parse.isError()) {
    return Error("Failed to parse JSON: " + parse.error());
  }

  // Create `QuotaInfo` Protobuf.
  Try<google::protobuf::RepeatedPtrField<Resource>> resources =
    ::protobuf::parse<google::protobuf::RepeatedPtrField<Resource>>(
        parse.get());

  if (resources.isError()) {
    return Error(
        "Error in parsing 'resources' in quota request: " + resources.error());
  }

  // Check that the request contains at least one resource.
  if (resources.get().size() == 0) {
    return Error("Quota request with empty resources");
  }

  // Get role of first resource.
  if (!resources.get().Get(0).has_role()) {
    return Error("Quota request without role specified");
  }
  if (resources.get().Get(0).role().empty()) {
    return Error("Quota request with empty role specified");
  }
  string role = resources.get().Get(0).role();


  QuotaInfo quota;
  quota.mutable_guarantee()->CopyFrom(resources.get());
  quota.set_role(role);

  return quota;
}

Try<Nothing> Master::QuotaHandler::validateQuotaInfo(
    const QuotaInfo& quotaInfo) const
{
  VLOG(1) << "Validating QuotaInfo";

  // The reference role for the quota request.
  string role = quotaInfo.role();

  foreach (const Resource& resource, quotaInfo.guarantee()) {
    // Check that each resource is valid.
    Option<Error> error = Resources::validate(resource);
    if (error.isSome()) {
      return Error(
        "Quota request with invalid resource: " + error.get().message);
    }

    // Check that Resource does not contain non-relevant fields for quota.

    if (resource.has_reservation()) {
      return Error("Quota request may not contain ReservationInfo");
    }
    if (resource.has_disk()) {
      return Error("Quota request may not contain DiskInfo");
    }
    if (resource.has_revocable()) {
      return Error("Quota request may not contain RevocableInfo");
    }

    // Check that the resource is scalar.
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
      // All role should the equal across a quota request.
      return Error("Quota request with different roles: '" + role +
                   "','" + resource.role() + "'");
    }
  }

  // Check that the role is known by the master.
  // TODO(alexr): Once we are able to dynamically add roles, we should stop
  // checking whether the requested role is known to the master, because an
  // operator may set quota for a role that is about to be introduced.
  if (!master->roles.contains(role)) {
    return Error("Quota request for unknown role: '" + role + "'");
  }

  return Nothing();
}


Option<Error> Master::QuotaHandler::checkSanity(const QuotaInfo& request) const
{
  VLOG(1) << "Checking sanity of a set quota request";

  CHECK(master->roles.contains(request.role()));

  // Calculate current resource allocation per role (including used resources,
  // outstanding offers, but not static reservations).
  // TODO(alexr): Count dynamic reservation in. Currently dynamic reservations
  // are not included in allocated or used resources (see MESOS-3338) and
  // there is no way to get the total amout of dynamically reserved resources
  // for a role without looping through all agents.
  Resources roleTotal = master->roles[request.role()]->resources();

  // Calculate the unsatisfied part of role quota. Quota is satisfied from '*'
  // resources, hence we remove the role via `flatten()`.
  Resources missingResources =
    Resources(request.guarantee()).flatten() - roleTotal.flatten();

  // Estimate total resources available in the cluster.
  Resources availableInCluster;
  foreachvalue (Slave* slave, master->slaves.registered) {
    // TODO(alexr): Consider counting REVOCABLE resources. Right now we do not
    // consider REVOCABLE resources for satisfying quota, but since it's up to
    // an allocator implementation, maybe we should count them in?

    if (!slave->connected || !slave->active) {
      continue;
    }

    Resources availableOnAgent =
      slave->totalResources -
      Resources::sum(slave->usedResources);

    // We do not use statically reserved resources to satisy quota.
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

  // If we reached this point, there are not enough resources in the cluster,
  // hence the request cannot be satisfied.
  return Error("Not enough available resources to satisfy quota request");
}


Future<http::Response> Master::QuotaHandler::set(
    const http::Request& request) const
{
  // Authenticate & authorize the request.
  // TODO(alexr): Check Master::Http::authenticate() for an example.

  // Check that the request type is POST which is guaranteed by the master.
  CHECK_EQ(request.method, "POST");

  // Validate the request contains valid JSON.
  Try<Nothing> validJson = validateRequest(request);
  if (validJson.isError()) {
    VLOG(1) << "Failed to validate set quota request: " << validJson.error();
    return BadRequest("Failed to validate set quota request: " +
                      validJson.error());
  }

  // Convert the request to QuotaInfo protobuf message.
  Try<QuotaInfo> quotaInfo = extractQuotaInfo(request);
  if (quotaInfo.isError()) {
    VLOG(1) << "Failed to convert request into QuotaInfo: "
            << quotaInfo.error();
    return BadRequest("Failed to convert request into QuotaInfo: " +
                      quotaInfo.error());
  }

  // Check that the QuotaInfo is a valid QuotaRequest.
  Try<Nothing> validQuota = validateQuotaInfo(quotaInfo.get());
  if (validQuota.isError()) {
    VLOG(1) << "Failed to validate set Quota request: " << quotaInfo.error();
    return BadRequest("Failed to validate set quota request: " +
                      validQuota.error());
  }


  // Check we are not updating an existing Quota.
  // TODO(joerg84): Update error message once quota update is in place.
  if (master->quotas.contains(quotaInfo.get().role())) {
    VLOG(1) << "Quota cannot be set for a role that already has quota";
    return BadRequest(
        "Quota cannot be set for a role that already has quota");
  }

  // Validate whether a quota request can be satisfied.
  Option<Error> sanityError = checkSanity(quotaInfo);
  if (sanityError.isSome()) {
    VLOG(1) << "Sanity check for set quota request failed: "
            << sanityError.get().message;

    return Conflict("Sanity check for set quota request failed: " +
                    sanityError.get().message);
  }

  // Populated master's quota-related local state. We do it before updating the
  // registry in order to make sure that we are not already trying to satisfy a
  // request for this role (since this is a multi-phase event).
  // NOTE: We do not need to remove quota for the role if the registry update
  // fails because in this case the master fails as well.
  Quota quota{quotaInfo.get()};
  master->quotas[quotaInfo.get().role()] = quota;

  // Update registry with the new quota.
  // TODO(alexr): MESOS-3165.

  // We are all set, grant the request.
  // TODO(alexr): Implement as per MESOS-3073.
  // TODO(alexr): This should be done after registry operation succeeds.

  // Notfify allocator.
  master->allocator->setQuota(quotaInfo.get().role(), quotaInfo.get());

  return OK();
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
