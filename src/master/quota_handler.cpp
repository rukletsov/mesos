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

#include <process/defer.hpp>
#include <process/future.hpp>
#include <process/http.hpp>

#include "master/master.hpp"

using namespace process;

using process::http::Accepted;
using process::http::BadRequest;
using process::http::Conflict;
using process::http::OK;

using std::string;


namespace mesos {
namespace internal {
namespace master {

using process::http::Response;
using process::http::Request;

// Dummy class QuotaInfo, shall be replaced by protobuf message after proper
// rebasing.
struct QuotaInfo
{
  std::string role;
  Resources guarantee;
};

// Quota tests.
// * Satisfiability tests
// * Two roles, two frameworks, one rejects the first offer, the other is
//   greedy and hijacks the cluster; no quota
// * Same as previous, but with the quota set


// Utility functions. In order to keep dependencies local to this .cpp file, we
// do not put them into QuotaHandler.
Try<QuotaInfo> validateQuotaRequest(const Request& request)
{
  // MESOS-3199.

  // Decode the request.
  if (request.method != "POST") {
    return BadRequest("Expecting POST");
  }

  // Parse the query string in the request body.
  Try<hashmap<string, string>> decode =
    process::http::query::decode(request.body);

  if (decode.isError()) {
    return BadRequest("Unable to decode query string: " + decode.error());
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
  return QuotaInfo;
}


// TODO(alexr): Add description for the method based on offline
// discussions and the design doc (optimistic check).
Option<Error> checkSatisfiability(
    const QuotaInfo& request,
    const Master* const master)
{
  // Calculate current resource allocation per role (including used resources,
  // outstanding offers, but not static reservations).
  Resources roleTotal = master->roles[role]->resources();

  // Count dynamic reservations in.
  // TODO(alexr): Dynamic reservations are not included in allocated or used
  // resources, see MESOS-3338.

  // Since we do an optimistick check and cannot know how an allocator actually
  // satisfies the quota, we include everything, except static reservations.
  // Currently allocated resources account towards quota.
  // TODO(alexr): Update this math based on quota design decisions.
  Resources missingResources = request.guarantee - roleTotal;

  // Estimate total resources available in the cluster.
  Resources unusedInCluster;
  foreachvalue (Slave* slave, master->slaves.registered) {
    Resources unusedOnAgent =
      slave->totalResources - Resources::sum(slave->usedResources);
    unusedInCluster += unusedOnAgent;
  }

  // If there are not enough resources in the cluster, reject
  // the request.
  if (!unusedInCluster.contains(missingResources)) {
    return Error("Not enough resources to satisfy quota request");
  }

  return None();
}


Future<Response> Master::QuotaHandler::request(const Request& request) const
{
  // TODO(alexr): First of all, authenticate request. Check
  // Master::Http::authenticate() for an example.

  // Next, validate and convert the request to internal protobuf Message.
  Try<QuotaInfo> validate = validateQuotaRequest(request);
  if (!validate.isSome()) {
    return BadRequest(validate.get().message);
  }

  // Check whether a quota request can be satisfied.
  Option<Error> satisfiability = checkSatisfiability(decode.get());
  if (satisfiability.isSome()) {
    return Conflict(satisfiability.get().message);
  }

  // 3. Update registry, MESOS-3165.

  // 4. Grant the request.
  return grantRequest(decode.get());
}





Future<Response> Master::QuotaHandler::grantRequest(
    const hashmap<string, string>& request) const
{
  // 1. Update master bookkeeping.

  // 2. Notfify allocator.


  return Accepted();
}


} // namespace master {
} // namespace internal {
} // namespace mesos {
