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

#include <stout/protobuf.hpp>

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


// TODO(alexr): Quota tests should come in two batches: allocator-dependent and
// allocator-independent.
// Allocator-dependent tests:
//   * Multiple frameworks in role with set quota;


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

  // Check all required (or optional, but logically required) fields are
  // present, including: role, resources, etc.

//  Option<Error> error = resource::validate(reserve.resources());
//  if (error.isSome()) {
//    return Error("Invalid resources: " + error.get().message);
//  }


  // Check there are no multiple resources of the same name, Check irrelevant
  // fields are not set: reservatin, disk, etc.

  // TODO(alexr): Once we are able to dynamically add roles, we should stop
  // checking whether the requested role is known to the master, because an
  // operator may set quota for a role that is about to be introduced.

  // Indicates validation is OK.
  Try<JSON::Array> parse =
    JSON::parse<JSON::Array>(decode.get().get("resources").get());

  google::protobuf::RepeatedPtrField<Resource> resources =
    ::protobuf::parse<google::protobuf::RepeatedPtrField<Resource>>(parse.get())
      .get();


  QuotaInfo quota;
  quota.mutable_guarantee()->CopyFrom(resources);
  quota.set_role(resources.begin()->role());

  return quota;
}


Option<Error> Master::QuotaHandler::validateSatisfiability(
    const QuotaInfo& request) const
{
  // Calculate current resource allocation per role (including used resources,
  // outstanding offers, but not static reservations).

  // If quota is requested for a future role, current resource usage is 0;
  Resources roleTotal = master->roles.contains(request.role())
                          ? master->roles[request.role()]->resources()
                          : Resources();

  // TODO(alexr): Count dynamic reservation in. Currently dynamic reservations
  // are not included in allocated or used resources, see MESOS-3338.

  // Exclude role, because we satisfy quota from '*' resources.
  Resources missingResources =
    Resources(request.guarantee()).flatten() - roleTotal.flatten();

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

  // Check a quota is not set per role.

  // Validate whether a quota request can be satisfied.
  Option<Error> satisfiability = validateSatisfiability(quota);
  if (satisfiability.isSome()) {
    return Conflict(satisfiability.get().message);
  }

  // Update registry with the new quota.
  // TODO(alexr): MESOS-3165.

  // We are all set, grant the request.
  // TODO(alexr): Implement as per MESOS-3073.
  // TODO(alexr): This should be done after registry operation succeeds.

  // Populated master's quota-related local state.
  master->quotas[quota.role()] = quota;

  // Notfify allocator.
  master->allocator->addQuota(quota.role(), quota);

  return OK();
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
