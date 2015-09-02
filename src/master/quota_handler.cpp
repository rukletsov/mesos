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

#include <mesos/master/quota.hpp>

#include <process/defer.hpp>
#include <process/future.hpp>
#include <process/http.hpp>

#include "master/master.hpp"

using std::string;

using process::Future;

using process::http::Accepted;
using process::http::BadRequest;
using process::http::Conflict;
using process::http::OK;

namespace mesos {
namespace internal {
namespace master {

using process::http::Response;
using process::http::Request;

using mesos::master::QuotaInfo;

// TODO(alexr): Quota tests.
// * Satisfiability tests
// * Two roles, two frameworks, one rejects the first offer, the other is
//   greedy and hijacks the cluster; no quota
// * Same as previous, but with the quota set


// Utility functions. In order to keep dependencies local to this .cpp file, we
// do not put them into QuotaHandler.

// TODO(alexr): Summarize what it does.
Try<QuotaInfo> validateQuotaRequest(const Request& request)
{
  // TODO(alexr): Implement as part per MESOS-3199.

  // Decode the request.
  if (request.method != "POST") {
    return Error("Expecting POST");
  }

  // Parse the query string in the request body.
  Try<hashmap<string, string>> decode =
    process::http::query::decode(request.body);

  if (decode.isError()) {
    return Error("Unable to decode query string: " + decode.error());
  }

  // TODO(alexr): Convert JSON -> Resources. Separate ticket will follow, see
  // MESOS-3312.
  // Resources resources(::protobuf::parse(request.get("resources").get()));

  // Check all required (or optional, but logically required) fields are
  // present, including: role, resources, etc.

  // We shouldn't check whether the provided role exists, because an operator
  // may set quota for a role that is about to be introduced (hope we'll be able
  // to dynamically add roles soon).

  // Indicates validation is OK.
  QuotaInfo quota;
  return quota;
}


Option<Error> Master::QuotaHandler::checkSatisfiability(
    const QuotaInfo& request) const
{
  // Calculate current resource allocation per role (including used resources,
  // outstanding offers, but not static reservations).
  Resources roleTotal = master->roles[request.role()]->resources();

  // TODO(alexr): Count dynamic reservation in. Currently dynamic reservations
  // are not included in allocated or used resources, see MESOS-3338.

  // TODO(alexr): Evolve this math together with quota feature evolution.
  Resources missingResources = request.guarantee() - roleTotal;

  // Estimate total resources available in the cluster.
  Resources unusedInCluster;
  foreachvalue (Slave* slave, master->slaves.registered) {
    // TODO(alexr): Consider counting REVOCABLE resources. Right now we do not
    // consider REVOCABLE resources for satisfying quota, but since it's up to
    // an allocator implementation, maybe we should count them in?
    Resources unusedOnAgent =
      slave->totalResources - Resources::sum(slave->usedResources);
    unusedInCluster += unusedOnAgent;

    // If we have found enough resources there is no need to continue.
    if (unusedInCluster.contains(missingResources)) {
      return None();
    }
  }

  // If we reached this point, there are not enough resources in the cluster,
  // hence the request cannot be satisfied.
  return Error("Not enough resources to satisfy quota request");
}


Future<Response> Master::QuotaHandler::grantRequest(
    const QuotaInfo& request) const
{
  // TODO(alexr): Implement as part per MESOS-3073.

  // 1. Update master bookkeeping.

  // 2. Notfify allocator.

  return Accepted();
}


Future<Response> Master::QuotaHandler::request(const Request& request) const
{
  // Authenticate & authorize the request.
  // TODO(alexr): Check Master::Http::authenticate() for an example.

  // Next, validate and convert the request to internal protobuf message.
  Try<QuotaInfo> validate = validateQuotaRequest(request);
  if (!validate.isSome()) {
    return BadRequest(validate.error());
  }

  const QuotaInfo& quota = validate.get();

  // Check whether a quota request can be satisfied.
  Option<Error> satisfiability = checkSatisfiability(quota);
  if (satisfiability.isSome()) {
    return Conflict(satisfiability.get().message);
  }

  // Update registry with the new quota.
  // TODO(alexr): MESOS-3165.

  // We are all set, grant the request.
  return grantRequest(quota);
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
