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
class QuotaInfo
{};

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

  // TODO(alexr): Extract role and resources.

  // TODO(alexr): Convert JSON -> Resources. Separate ticket will follow.

  // Check all required (or optional, but logically required) fields are
  // present, including: role, resources, etc.

  // We shouldn't check whether the provided role exists, because an operator
  // may set quota for a role that is about to be introduced (hope we'll be able
  // to dynamically add roles soon).

  // Indicates validation is OK.
  return QuotaInfo;
}


Future<Response> Master::QuotaHandler::request(const Request& request) const
{
  // TODO(alexr): First of all, authenticate request. Check
  // Master::Http::authenticate() for an example.



  // In an endpoint handler there are three possible outcomes:
  //   1) A bug in handler => future is not ready => HttpProxy sends 503.
  //   2) The request cannot be fulfilled => Future is ready, but the
  //     the response returned by the handler is 4** or 5**.
  //   3) Everything is fine => handler returns 2**.
  // If we want to split request processing into several stages (and
  // functions), we have several options how to handle case 2. The nicest
  // and most natural would be to fail a corresponding future if a stage
  // cannot be processed successfully. The problem with this approach is
  // that we cannot fail futures and assign an arbitrary object (at least
  // now), hence we should dispatch on failure message and generate when
  // generating the final response.
  //
  // The approach we take is perform logically related checks in separate
  // functions. Since we cannot wrap arbitrary objects into Error, we should
  // delegate creating Response objects to the caller, therefore there should
  // be one response type per check function.

  Try<QuotaInfo> validate = validateQuotaRequest(request);
  if (!validate.isSome()) {
    return BadRequest(validate.get().message);
  }

  Option<Error> satisfiability = checkSatisfiability(decode.get());
  if (satisfiability.isSome()) {
    return Conflict(satisfiability.get().message);
  }

  // 3. Update registry, MESOS-3165.

  // 4. Grant the request.
  return grantRequest(decode.get());
}


Option<Error> Master::QuotaHandler::checkSatisfiability(
    const QuotaInfo& request) const
{
  // Cache No need to check whether a key exsists, we have done that during
  // validation.
  const string& role = request.get("role").get();

  // TODO(alexr): Convert resources JSON to resources, or, actually, re-use
  // conversion done during previous steps.
  // const auto& resources = request.get("resources").get();
  Resources requestResources;

  // Calculate current resource allocation per role.
  Resources roleTotal = master->roles[role]->resources();

  // Currently allocated resources account towards quota.
  // TODO(alexr): Depending on what we account towards role quota, update this
  // math.
  Resources missingResources = requestResources - roleTotal;

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
