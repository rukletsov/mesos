// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License

#include "master/master.hpp"

#include <vector>

#include <google/protobuf/repeated_field.h>

#include <mesos/resources.hpp>

#include <mesos/quota/quota.hpp>

#include <process/defer.hpp>
#include <process/future.hpp>
#include <process/http.hpp>
#include <process/owned.hpp>

#include <stout/json.hpp>
#include <stout/protobuf.hpp>
#include <stout/stringify.hpp>
#include <stout/strings.hpp>
#include <stout/utils.hpp>

#include "logging/logging.hpp"

#include "master/quota.hpp"
#include "master/registrar.hpp"

namespace http = process::http;

using google::protobuf::RepeatedPtrField;

using http::Accepted;
using http::BadRequest;
using http::Conflict;
using http::OK;
using http::Unauthorized;

using mesos::quota::QuotaInfo;

using process::Future;
using process::Owned;

using std::string;
using std::vector;

namespace mesos {
namespace internal {
namespace master {

// Creates a `QuotaInfo` protobuf from the quota request.
static Try<QuotaInfo> createQuotaInfo(RepeatedPtrField<Resource> resources)
{
  VLOG(1) << "Constructing QuotaInfo from resources protobuf";

  QuotaInfo quota;

  // Set the role if we have one. Since all roles must be the same, pick
  // any, e.g. the first one.
  if (resources.size() > 0) {
     quota.set_role(resources.begin()->role());
  }

  // Check that all roles are set and equal.
  // TODO(alexr): Remove this check as per MESOS-4058.
  foreach (const Resource& resource, resources) {
    if (resource.role() != quota.role()) {
      return Error(
          "Resources with different roles: '" + quota.role() + "', '" +
          resource.role() + "'");
    }
  }

  // Remove the role from each resource.
  // TODO(alexr): Remove this as per MESOS-4058. Corresponding validation
  // is in `internal::master::quota::validation::quotaInfo()`.
  foreach (Resource& resource, resources) {
    resource.clear_role();
  }

  quota.mutable_guarantee()->CopyFrom(resources);

  return quota;
}


Option<Error> Master::QuotaHandler::capacityHeuristic(
    const QuotaInfo& request) const
{
  VLOG(1) << "Performing capacity heuristic check for a set quota request";

  // This should have been validated earlier.
  CHECK(master->isWhitelistedRole(request.role()));
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
  CHECK(master->isWhitelistedRole(role));

  int frameworksInRole = 0;
  if (master->activeRoles.contains(role)) {
    Role* roleState = master->activeRoles[role];
    foreachvalue (const Framework* framework, roleState->frameworks) {
      if (framework->connected && framework->active) {
        ++frameworksInRole;
      }
    }
  }

  // The resources recovered by rescinding outstanding offers.
  Resources rescinded;

  int visitedAgents = 0;

  // Because resources are allocated in the allocator, there can be a race
  // between rescinding and allocating. This race makes it hard to determine
  // the exact amount of offers that should be rescinded in the master.
  //
  // We pessimistically assume that what seems like "available" resources
  // in the allocator will be gone. We greedily rescind all offers from an
  // agent at once until we have rescinded "enough" offers. Offers containing
  // resources irrelevant to the quota request may be rescinded, as we
  // rescind all offers on an agent. This is done to maintain the
  // coarse-grained nature of agent offers, and helps reduce fragmentation of
  // offers.
  //
  // Consider a quota request for role `role` for `requested` resources.
  // There are `numFiR` frameworks in `role`. Let `rescinded` be the total
  // number of rescinded resources and `numVA` be the number of visited
  // agents, from which at least one offer has been rescinded. Then the
  // algorithm can be summarized as follows:
  //
  //   while (there are agents with outstanding offers) do:
  //     if ((`rescinded` contains `requested`) && (`numVA` >= `numFiR`) break;
  //     fetch an agent `a` with outstanding offers;
  //     rescind all outstanding offers from `a`;
  //     update `rescinded`, inc(numVA);
  //   end.
  foreachvalue (const Slave* slave, master->slaves.registered) {
    // If we have rescinded offers with at least as many resources as the
    // quota request resources, then we are done.
    if (rescinded.contains(request.guarantee()) &&
        (visitedAgents >= frameworksInRole)) {
      break;
    }

    // As in the capacity heuristic, we do not consider disconnected or
    // inactive agents, because they do not participate in resource
    // allocation.
    if (!slave->connected || !slave->active) {
      continue;
    }

    // TODO(alexr): Consider only rescinding from agents that have at least
    // one resource relevant to the quota request.

    // Rescind all outstanding offers from the given agent.
    bool agentVisited = false;
    foreach (Offer* offer, utils::copy(slave->offers)) {
      master->allocator->recoverResources(
          offer->framework_id(), offer->slave_id(), offer->resources(), None());

      rescinded += offer->resources();
      master->removeOffer(offer, true);
      agentVisited = true;
    }

    if (agentVisited) {
      ++visitedAgents;
    }
  }
}


Future<http::Response> Master::QuotaHandler::set(
    const http::Request& request) const
{
  VLOG(1) << "Setting quota from request: '" << request.body << "'";

  // Authenticate the request.
  Result<Credential> credential = master->http.authenticate(request);
  if (credential.isError()) {
    return Unauthorized("Mesos master", credential.error());
  }

  // Check that the request type is POST which is guaranteed by the master.
  CHECK_EQ("POST", request.method);

  // Validate request and extract JSON.
  // TODO(alexr): Create a type (e.g. a protobuf) for the request JSON. If we
  // move the `force` field out of the request JSON, we can reuse `QuotaInfo`.
  Try<JSON::Object> parse = JSON::parse<JSON::Object>(request.body);
  if (parse.isError()) {
    return BadRequest(
        "Failed to parse set quota request JSON '" + request.body + "': " +
        parse.error());
  }

  Result<JSON::Array> resourcesJSON =
    parse.get().find<JSON::Array>("resources");

  if (resourcesJSON.isError()) {
    // An `Error` usually indicates that a search string is malformed
    // (which is not the case here), however it may also indicate that
    // the `resources` field is not an array.
    return BadRequest(
        "Failed to extract 'resources' from set quota request JSON '" +
        request.body + "': " + resourcesJSON.error());
  }

  if (resourcesJSON.isNone()) {
    return BadRequest(
        "Failed to extract 'resources' from set quota request JSON '" +
        request.body + "': Field is missing");
  }

  // Create protobuf representation of resources.
  Try<RepeatedPtrField<Resource>> resources =
    ::protobuf::parse<RepeatedPtrField<Resource>>(resourcesJSON.get());

  if (resources.isError()) {
    return BadRequest(
        "Failed to parse 'resources' from set quota request JSON '" +
        request.body + "': " + resources.error());
  }

  // Create the `QuotaInfo` protobuf message from the request JSON.
  Try<QuotaInfo> create = createQuotaInfo(resources.get());
  if (create.isError()) {
    return BadRequest(
        "Failed to create 'QuotaInfo' from set quota request JSON '" +
        request.body + "': " + create.error());
  }

  const QuotaInfo& quotaInfo = create.get();

  // Check that the `QuotaInfo` is a valid quota request.
  Try<Nothing> validate = quota::validation::quotaInfo(quotaInfo);
  if (validate.isError()) {
    return BadRequest(
        "Failed to validate set quota request JSON '" + request.body + "': " +
        validate.error());
  }

  // Check that the role is on the role whitelist, if it exists.
  if (!master->isWhitelistedRole(quotaInfo.role())) {
    return BadRequest(
        "Failed to validate set quota request JSON '" + request.body +
        "': Unknown role '" + quotaInfo.role() + "'");
  }

  // Check that we are not updating an existing quota.
  // TODO(joerg84): Update error message once quota update is in place.
  if (master->quotas.contains(quotaInfo.role())) {
    return BadRequest(
        "Failed to validate set quota request JSON '" + request.body +
        "': Can not set quota for a role that already has quota");
  }

  // The force flag can be used to overwrite the `capacityHeuristic` check.
  Result<JSON::Boolean> force = parse.get().find<JSON::Boolean>("force");
  if (force.isError()) {
    // An `Error` usually indicates that a search string is malformed
    // (which is not the case here), however it may also indicate that
    // the `force` field is not a boolean.
    return BadRequest(
        "Failed to extract 'force' from set quota request JSON '" +
        request.body + "': " + force.error());
  }

  const bool forced = force.isSome() ? force.get().value : false;

  // Extract principal from request credentials.
  Option<string> principal =
    credential.isSome() ? credential.get().principal() : Option<string>::none();

  return authorize(principal, quotaInfo.role())
    .then(defer(master->self(), [=](bool authorized) -> Future<http::Response> {
      if (!authorized) {
        return Unauthorized("Mesos master");
      }

      return _set(quotaInfo, forced);
    }));
}


Future<http::Response> Master::QuotaHandler::_set(
    const QuotaInfo& quotaInfo,
    bool forced) const
{
  if (forced) {
    VLOG(1) << "Using force flag to override quota capacity heuristic check";
  } else {
    // Validate whether a quota request can be satisfied.
    Option<Error> error = capacityHeuristic(quotaInfo);
    if (error.isSome()) {
      return Conflict(
          "Heuristic capacity check for set quota request failed: " +
          error.get().message);
    }
  }

  // Populate master's quota-related local state. We do this before updating
  // the registry in order to make sure that we are not already trying to
  // satisfy a request for this role (since this is a multi-phase event).
  // NOTE: We do not need to remove quota for the role if the registry update
  // fails because in this case the master fails as well.
  master->quotas[quotaInfo.role()] = Quota{quotaInfo};

  // Update the registry with the new quota and acknowledge the request.
  return master->registrar->apply(Owned<Operation>(
      new quota::UpdateQuota(quotaInfo)))
    .then(defer(master->self(), [=](bool result) -> Future<http::Response> {
      // See the top comment in "master/quota.hpp" for why this check is here.
      CHECK(result);

      master->allocator->setQuota(quotaInfo.role(), quotaInfo);

      // Rescind outstanding offers to facilitate satisfying the quota request.
      // NOTE: We set quota before we rescind to avoid a race. If we were to
      // rescind first, then recovered resources may get allocated again
      // before our call to `setQuota` was handled.
      // The consequence of setting quota first is that (in the hierarchical
      // allocator) it will trigger an allocation. This means the rescinded
      // offer resources will only be available to quota once another
      // allocation is invoked.
      // This can be resolved in the future with an explicit allocation call,
      // and this solution is preferred to having the race described earlier.
      rescindOffers(quotaInfo);

      return OK();
    }));
}


Future<http::Response> Master::QuotaHandler::remove(
    const http::Request& request) const
{
  VLOG(1) << "Removing quota for request path: '" << request.url.path << "'";

    // Authenticate the request.
  Result<Credential> credential = master->http.authenticate(request);
  if (credential.isError()) {
    return Unauthorized("Mesos master", credential.error());
  }

  // TODO(nfnt): Authorize the request.

  // Check that the request type is DELETE which is guaranteed by the master.
  CHECK_EQ("DELETE", request.method);

  // Extract role from url.
  vector<string> tokens = strings::tokenize(request.url.path, "/");

  // Check that there are exactly 3 parts: {master,quota,'role'}.
  if (tokens.size() != 3u) {
    return BadRequest(
        "Failed to parse request path '" + request.url.path +
        "': 3 tokens ('master', 'quota', 'role') required, found " +
        stringify(tokens.size()) + " token(s)");
  }

  // Check that "quota" is the second to last token.
  if (tokens.end()[-2] != "quota") {
    return BadRequest(
        "Failed to parse request path '" + request.url.path +
        "': Missing 'quota' endpoint");
  }

  const string& role = tokens.back();

  // Check that the role is on the role whitelist, if it exists.
  if (!master->isWhitelistedRole(role)) {
    return BadRequest(
        "Failed to validate remove quota request for path '" +
        request.url.path +"': Unknown role '" + role + "'");
  }

  // Check that we are removing an existing quota.
  if (!master->quotas.contains(role)) {
    return BadRequest(
        "Failed to remove quota for path '" + request.url.path +
        "': Role '" + role + "' has no quota set");
  }

  // Remove quota from the quota-related local state. We do this before
  // updating the registry in order to make sure that we are not already
  // trying to remove quota for this role (since this is a multi-phase event).
  // NOTE: We do not need to restore quota for the role if the registry
  // update fails because in this case the master fails as well and quota
  // will be restored automatically during the recovery.
  master->quotas.erase(role);

  // Update the registry with the removed quota and acknowledge the request.
  return master->registrar->apply(Owned<Operation>(
      new quota::RemoveQuota(role)))
    .then(defer(master->self(), [=](bool result) -> Future<http::Response> {
      // See the top comment in "master/quota.hpp" for why this check is here.
      CHECK(result);

      master->allocator->removeQuota(role);

      return OK();
    }));
}


Future<bool> Master::QuotaHandler::authorize(
    const Option<string>& principal,
    const string& role) const
{
  if (master->authorizer.isNone()) {
    return true;
  }

  LOG(INFO) << "Authorizing principal '"
            << (principal.isSome() ? principal.get() : "ANY")
            << "' to request quota for role '" << role << "'";

  mesos::ACL::SetQuota request;

  if (principal.isSome()) {
    request.mutable_principals()->add_values(principal.get());
  } else {
    request.mutable_principals()->set_type(mesos::ACL::Entity::ANY);
  }

  request.mutable_roles()->add_values(role);

  return master->authorizer.get()->authorize(request);
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
