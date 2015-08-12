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

using process::http::Conflict;
using process::http::OK;


namespace mesos {
namespace internal {
namespace master {

using process::http::Response;
using process::http::Request;


// TODO(alexr): Describe the pipelining for request processing.
Future<Response> Master::QuotaHandler::request(const Request& request) const
{
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
  // The approach I suggest here is to return Option<Response> from stage
  // processing functions, where None() indicates success and triggers
  // next stage, while a Response means we should not continue and just
  // propogate to the end of the pipeline.
  return requestValidate(request)
    .then(
       defer(master->self(),
             [=](const Option<Response>& response) -> Future<Option<Response>> {
         if (response.isSome()) {
           // If a response was generated, something went wrong and we just
           // propagate that respond to the client.
           return response;
         }
         return requestCheckSatisfiability(request);
       }))
    .then(
      defer(master->self(),
            [=](const Option<Response>& response) -> Future<Response> {
        if (response.isSome()) {
          // If a response was generated, something went wrong and we just
          // propagate that respond to the client.
          return response.get();
        }
        return requestGrant(request);
      }));
}


Future<Option<Response>> Master::QuotaHandler::requestValidate(
    const Request& request) const
{
  // Indicates validation is OK, hence no response is generated here.
  return None();
}


// TODO(alexr): Add tests for satisfiablity.
Future<Option<Response>> Master::QuotaHandler::requestCheckSatisfiability(
    const Request& request) const
{
  // Calculate current resource allocation per role.
  Resources roleTotal = master->roles["role"]->resources();

  // Create an operation based on resources from quota request.
  // Currently allocated resources account towards quota.
  Resources requestResources;
  Resources missingResources = requestResources - roleTotal;

  // Estimate total resources available in the cluster.
  Resources clusterUnused;
  foreachvalue (Slave* slave, master->slaves.registered) {
    Resources unusedOnSlave =
      slave->totalResources - Resources::sum(slave->usedResources);
    clusterUnused += unusedOnSlave;
  }

  // If there are not enough resources in the cluster, reject
  // the request.
  if (!clusterUnused.contains(missingResources)) {
    return process::http::Conflict("Not enough resources");
  }

  return None();
}


Future<Response> Master::QuotaHandler::requestGrant(const Request& request) const
{
  return process::http::Accepted();
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
