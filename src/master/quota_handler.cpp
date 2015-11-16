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

#include <stout/json.hpp>
#include <stout/protobuf.hpp>
#include <stout/utils.hpp>

#include "logging/logging.hpp"

#include "master/quota.hpp"

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

static Try<google::protobuf::RepeatedPtrField<Resource>> parseResources(
    const std::string& _resources)
{
  Try<JSON::Array> parse = JSON::parse<JSON::Array>(_resources);
  if (parse.isError()) {
    return Error("Error in parsing 'resources' string ('" + _resources +
                 "'): " + parse.error());
  }

  // Create Protobuf representation of resources.
  Try<google::protobuf::RepeatedPtrField<Resource>> resources =
    ::protobuf::parse<google::protobuf::RepeatedPtrField<Resource>>(
        parse.get());

  if (resources.isError()) {
    return Error(
        "Error in parsing 'resources' JSON array: " + resources.error());
  }

  return resources.get();
}


// Creates a `QuotaInfo` protobuf from the quota request.
static Try<QuotaInfo> createQuotaInfo(
    const google::protobuf::RepeatedPtrField<Resource>& resources)
{
  VLOG(1) << "Constructing QuotaInfo from resources protobuf";

  QuotaInfo quota;
  quota.mutable_guarantee()->CopyFrom(resources);

  // Set the role if we have one.
  if (resources.size() > 0) {
     quota.set_role(resources.begin()->role());
  }

  return quota;
}


Option<Error> Master::QuotaHandler::capacityHeuristic(
    const QuotaInfo& request) const
{
  VLOG(1) << "Performing capacity heuristic check for a set quota request";

  // This should have been validated earlier.
  CHECK(master->roles.contains(request.role()));
  CHECK(!master->quotas.contains(request.role()));

  // Calculate the total amount of resources requested by all quotas
  // (including the request) in the cluster.
  // NOTE: We have validated earlier that the quota for the role in the
  // request does not exist, hence `master->quotas` is guaranteed not to
  // contain the request role's quota yet.
  // TODO(alexr): Relax this constraint once we allow updating quotas.
  Resources totalQuota = request.guarantee();
  foreachvalue (const Quota& quota, master->quotas) {
    totalQuota += quota.info.guarantee();
  }

  // Remove roles via `flatten()` to facilitate resource math.
  totalQuota = totalQuota.flatten();

  // Determine whether the total quota, including the new request, does
  // not exceed the sum of non-static cluster resources.
  // NOTE: We do not necessarily calculate the full sum of non-static
  // cluster resources. We apply the early termination logic as it can
  // reduce the cost of the function significantly. This early exit does
  // not influence the declared inequality check.
  Resources nonStaticClusterResources;
  foreachvalue (Slave* slave, master->slaves.registered) {
    // We do not consider disconnected or inactive agents, because they
    // do not participate in resource allocation.
    if (!slave->connected || !slave->active) {
      continue;
    }

    // NOTE: Dynamic reservations are not excluded here because they do
    // not show up in `SlaveInfo` resources. In contrast to static
    // reservations, dynamic reservations may be unreserved at any time,
    // hence making resources available for quota'ed frameworks.
    Resources nonStaticAgentResources =
      Resources(slave->info.resources()).unreserved();

    nonStaticClusterResources += nonStaticAgentResources;

    // If we have found enough resources to satisfy the inequality, then
    // we can return early.
    if (nonStaticClusterResources.contains(totalQuota)) {
      return None();
    }
  }

  // If we reached this point, there are not enough available resources
  // in the cluster, hence the request does not pass the heuristic.
  return Error(
      "Not enough available cluster capacity to reasonably satisfy quota "
      "request; the force flag can be used to override this check");
}


void Master::QuotaHandler::rescindOffers(const QuotaInfo& request) const
{
  const string& role = request.role();

  // This should have been validated earlier.
  CHECK(master->roles.contains(role));

  int numVisitedAgents = 0;

  int numQuotaedFrameworks = 0;
  foreachvalue (const Framework* framework, master->roles[role]->frameworks) {
    if (framework->connected && framework->active) {
      ++numQuotaedFrameworks;
    }
  }

  // The resources recovered by rescinding outstanding offers.
  Resources rescinded;

  // Because resources are allocated in the allocator, there can be a race
  // between rescinding and allocating. This race makes it hard to determine
  // the exact amount of offers that should be rescinded in the master.
  //
  // We pessimistically assume that what seems like "available" resources
  // in the allocator will be gone. We greedily rescind all offers from an
  // agent at time until we have rescinded "enough" offers. Offers that do
  // not contribute to satisfying quota request will be rescinded regardless.
  //
  // Consider a quota request for role `role` for `requested` resources.
  // There are `numQF` frameworks in `role`. Let `rescinded` be the total
  // number of rescinded resources and `numVA` be the number of visited
  // agents. Then the algorithm can be summarized as follows:
  //
  //   while (there are agents with outstanding offers) do:
  //     if ((`rescinded` contains `requested`) && (`numVA` >= `numQF`) break;
  //     fetch an agent `a` with outstanding offers;
  //     rescind all outstanding offers from `a`;
  //     update `rescinded`, inc(numVA);
  //   end.
  foreachvalue (const Slave* slave, master->slaves.registered) {
    // If we have rescinded enough offers to cover for quota resources,
    // we are done.
    if (rescinded.contains(Resources(request.guarantee()).flatten()) &&
        (numVisitedAgents >= numQuotaedFrameworks)) {
      break;
    }

    // As in the capacity heuristic, we do not consider disconnected or
    // inactive agents, because they do not participate in resource
    // allocation.
    if (!slave->connected || !slave->active) {
      continue;
    }

    // Rescind all outstanding offers from the given agent.
    foreach (Offer* offer, utils::copy(slave->offers)) {
      // There is still a race here: recovered resources may be allocated
      // right after, rendering them unavailable for quota. Even if we
      // explicitly pass `Filters()` (which has a default `refuse_sec` of
      // five seconds) instead of `None()` here, it will not necessarily
      // help us win the race against `allocate()`, because filter is set
      // for a certain framework.
      //
      // TODO(alexr): To eliminate the race completely, we could specify
      // multiple operations that must be performed atomically in the
      // allocator. An alternative approach is to rescind in the allocator.
      master->allocator->recoverResources(
          offer->framework_id(), offer->slave_id(), offer->resources(), None());

      rescinded += offer->resources();
      master->removeOffer(offer, true);
    }

    ++numVisitedAgents;
  }
}


Future<http::Response> Master::QuotaHandler::set(
    const http::Request& request) const
{
  VLOG(1) << "Setting quota from request: '" << request.body << "'";

  // Authenticate and authorize the request.
  // TODO(alexr): Check Master::Http::authenticate() for an example.

  // Check that the request type is POST which is guaranteed by the master.
  CHECK_EQ("POST", request.method);

  // Validate request and extract JSON.

  Try<hashmap<string, string>> decode = http::query::decode(request.body);
  if (decode.isError()) {
    return BadRequest("Failed to decode set quota request query string ('" +
                      request.body + "'): " +
                      decode.error());
  }

  hashmap<string, string> values = decode.get();

  if (!values.contains("resources")) {
    return BadRequest("Failed to parse set quota request query string ('" +
                      request.body + "'): Missing 'resources'");
  }

  Try<google::protobuf::RepeatedPtrField<Resource>> resources =
    parseResources(values["resources"]);

  if (resources.isError()) {
    return BadRequest("Failed to parse set quota request query string ('" +
                      request.body + "'): " + resources.error());
  }

  // Create the `QuotaInfo` protobuf message from the request JSON.
  Try<QuotaInfo> create = createQuotaInfo(resources.get());
  if (create.isError()) {
    return BadRequest("Failed to create QuotaInfo from set quota request "
                      "query string '(" + request.body + "'): " +
                      create.error());
  }

  // Check that the `QuotaInfo` is a valid quota request.
  Try<Nothing> validate = quota::validation::quotaInfo(create.get());
  if (validate.isError()) {
    return BadRequest("Failed to validate set quota request query string: ('" +
                      request.body + "'): " + validate.error());
  }

  // Check that the role is known by the master.
  // TODO(alexr): Once we are able to dynamically add roles, we should stop
  // checking whether the requested role is known to the master, because an
  // operator may set quota for a role that is about to be introduced.
  if (!master->roles.contains(create.get().role())) {
    return BadRequest("Failed to validate set quota request query string: ('" +
                      request.body +"')': Unknown role: '" +
                      create.get().role() + "'");
  }

  // Check that we are not updating an existing quota.
  // TODO(joerg84): Update error message once quota update is in place.
  if (master->quotas.contains(create.get().role())) {
    return BadRequest("Failed to validate set quota request query string: ('" +
                      request.body + "')': "
                      "Can not set quota for a role that already has quota");
  }

  const QuotaInfo& quotaInfo = create.get();

  // Validate whether a quota request can be satisfied.
  Option<Error> error = capacityHeuristic(quotaInfo);
  if (error.isSome()) {
    return Conflict("Heuristic capacity check for set quota request failed: " +
                    error.get().message);
  }

  // Populate master's quota-related local state. We do this before updating
  // the registry in order to make sure that we are not already trying to
  // satisfy a request for this role (since this is a multi-phase event).
  // NOTE: We do not need to remove quota for the role if the registry update
  // fails because in this case the master fails as well.
  master->quotas[quotaInfo.role()] = Quota{quotaInfo};

  // Update the registry with the new quota.
  // TODO(alexr): MESOS-3165.

  // We are all set, grant the request.
  // TODO(alexr): Implement as per MESOS-3073.
  // TODO(alexr): This should be done after registry operation succeeds.

  // Rescind enough outstanding offers to satisfy the quota request.
  rescindOffers(quotaInfo);

  // Notfify allocator.
  master->allocator->setQuota(quotaInfo.role(), quotaInfo);

  return OK();
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
