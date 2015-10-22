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

#include "master/master.hpp"

#include "logging/logging.hpp"

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

using mesos::quota::QuotaInfo;

// Utility functions. In order to keep dependencies local to this .cpp file, we
// do not put them into QuotaHandler.

Try<QuotaInfo> Master::QuotaHandler::validateQuotaRequest(
    const Request& request) const
{
  VLOG(1) << "Validating Quota Request";

  // Check that the request type is POST.
  if (request.method != "POST") {
    return Error("QuotaRequest should be POST, got '" + request.method + "'");
  }

  // Check whether the request is valid json.
  Try<JSON::Object> parse = JSON::parse<JSON::Object>(request.body);
  if (parse.isError()) {
    return Error(
        "Error in parsing Quota Request: " + parse.error());
  }

  const JSON::Array array = parse.get().values["resources"].as<JSON::Array>();
  string role;
  foreach (const JSON::Value& value, array.values) {
    // Check whether it is a valid resource message.
    Try<Resource> resource = ::protobuf::parse<Resource>(value);
    if (resource.isError()) {
      return Error(
          "Error in parsing 'resources' in Quota Request: " + resource.error());
    }

    // Check that resource is valid.
    Option<Error> error = Resources::validate(resource.get());
    if (error.isSome()) {
      return Error(
        "Quota Request with invalid resources: " + error.get().message);
    }

    // Checking that Resource does not contain non-relevant fields.
    // Check that Request does not contain ReservationInfo.
    if (resource.get().has_reservation()) {
      return Error("Quota Request may not contain ReservationInfo.");
    }
    // Check that Request does not contain DiskInfo.
    if (resource.get().has_disk()) {
      return Error("Quota Request may not contain DiskInfo.");
    }
    // Check that Request does not contain RevocableInfo.
    if (resource.get().has_revocable()) {
      return Error("Quota Request may not contain RevocableInfo.");
    }

    // Check all roles are set and equal.
    if (!resource.get().has_role()) {
      return Error("Quota Request without role specified.");
    }
    // Store first encountered role as reference.
    if (role.empty()) {
      role = resource.get().role();
    }
    // Ensure role is equal to reference role.
    if (role != resource.get().role() ) {
      return Error("Quota Request with different roles.");
    }

    // Check that all resources are scalar.
    if (resource.get().type() != Value::SCALAR) {
      return Error(
          "Quota Request including non-scalar resources.");
    }
  }

  // Check that the role is kown by the master.
  // TODO(alexr): Once we are able to dynamically add roles, we should stop
  // checking whether the requested role is known to the master, because an
  // operator may set quota for a role that is about to be introduced.
  if (!master->roles.contains(role)) {
    return Error("Quota Request for unknown role.");
  }

  // Create Quota Info Protobuf.
  google::protobuf::RepeatedPtrField<Resource> resources =
    ::protobuf::parse<google::protobuf::RepeatedPtrField<Resource>>(array)
      .get();

  QuotaInfo quota;
  quota.mutable_guarantee()->CopyFrom(resources);
  quota.set_role(role);

  return quota;
}


Future<Response> Master::QuotaHandler::set(const Request& request) const
{
  // Authenticate & authorize the request.
  // TODO(alexr): Check Master::Http::authenticate() for an example.

  // Next, validate and convert the request to internal protobuf message.
  Try<QuotaInfo> validate = validateQuotaRequest(request);
  if (!validate.isSome()) {
    return BadRequest(validate.error());
  }

  const QuotaInfo& quota = validate.get();

  // Check we are not updating an existing Quota.
  if (master->roles[quota.role()]->quotaInfo.isSome()) {
    return BadRequest(
        "Existing Quota for role. Use PUT /master/quota/role to update.");
  }

  // Check a quota is not set per role.

  // Validate whether a quota request can be satisfied.
  // TODO(alexr): Implement as per MESOS-3073.

  // Update registry with the new quota.
  // TODO(alexr): MESOS-3165.

  // We are all set, grant the request.
  // TODO(alexr): Implement as per MESOS-3073.
  // TODO(alexr): This should be done after registry operation succeeds.

  // Populated master's quota-related local state.
  master->quotas[quota.role()] = quota;

  // Notfify allocator.
  master->allocator->setQuota(quota.role(), quota);

  return OK();
}


} // namespace master {
} // namespace internal {
} // namespace mesos {
