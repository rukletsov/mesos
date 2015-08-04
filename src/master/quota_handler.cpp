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
#include <process/dispatch.hpp>
#include <process/future.hpp>
#include <process/http.hpp>

#include "master/quota_handler.hpp"

using namespace process;

using process::http::Conflict;
using process::http::Request;
using process::http::Response;


namespace mesos {
namespace internal {
namespace master {

// TODO(alexr): Add tests for satisfiablity.
// TODO(alexr): Add description for the method based on offline
// discussions and the design doc.
Future<Response> QuotaHandler::request(
    const process::http::Request& request)
{
  return requestValidate(request)
    .then(defer(master->self(), [this](const Request& request) {
      return this->requestCheckSatisfiability(request);
    }));
}

Future<Response> QuotaHandler::requestCheckSatisfiability(
    const Request& request)
{
  //


  // Calculate current resource allocation per role.
  Resources roleTotal = master->roles["role"]->resources();

  // Create an operation based on resources from quota request.
  // Currently allocated resources account towards quota.
  Resources requestResources;
  Resources missingResources = requestResources - roleTotal;

  // Estimate total resources available in the cluster.
  Resources clusterUnused;
  foreachvalue (Slave* slave, master->slaves.registered.ids) {
    Resources unusedOnSlave =
      slave->totalResources - Resources::sum(slave->usedResources);
    clusterUnused += unusedOnSlave;
  }

  // If there are not enough resources in the cluster, reject
  // the request.
  if (!clusterUnused.contains(missingResources)) {
    return process::http::Conflict(apply.error());
  }

  return OK();
}



} // namespace master {
} // namespace internal {
} // namespace mesos {
