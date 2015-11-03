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

using std::string;

using process::Future;

using process::http::Accepted;
using process::http::BadRequest;
using process::http::Conflict;
using process::http::OK;

namespace mesos {
namespace internal {
namespace master {

using mesos::quota::QuotaInfo;

using process::http::Request;
using process::http::Response;


Try<QuotaInfo> Master::QuotaHandler::validateQuotaRequest(
    const Request& request) const
{
  VLOG(1) << "Validating quota request: '" << request.body << "'";

  // Parse the query string in the request body.
  Try<hashmap<string, string>> decode =
    process::http::query::decode(request.body);
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
  string role;
  foreach (const JSON::Value& value, parse.get().values) {
    // Check whether it is a valid resource message.
    Try<Resource> resource = ::protobuf::parse<Resource>(value);
    if (resource.isError()) {
      return Error(
          "Error in parsing 'resources' in quota request: " + resource.error());
    }
    // Check that resource is valid.
    Option<Error> error = Resources::validate(resource.get());
    if (error.isSome()) {
      return Error(
        "Quota request with invalid resources: " + error.get().message);
    }

    // Check that Resource does not contain non-relevant fields for quota.

    if (resource.get().has_reservation()) {
      return Error("Quota request may not contain ReservationInfo");
    }
    if (resource.get().has_disk()) {
      return Error("Quota request may not contain DiskInfo");
    }
    if (resource.get().has_revocable()) {
      return Error("Quota request may not contain RevocableInfo");
    }

    // Check all roles are set and equal.

    if (!resource.get().has_role()) {
      return Error("Quota request without role specified");
    }
    if (role.empty()) {
      // Store first encountered role as reference.
      role = resource.get().role();
    }
    // Ensure role is equal to reference role.
    if (role != resource.get().role() ) {
      return Error("Quota request with different roles: '" + role +
                   "','" + resource.get().role() + "'");
    }

    // Check that the resource is scalar.
    if (resource.get().type() != Value::SCALAR) {
      return Error(
          "Quota request including non-scalar resources");
    }
  }

  // Check that the role is kown by the master.
  // TODO(alexr): Once we are able to dynamically add roles, we should stop
  // checking whether the requested role is known to the master, because an
  // operator may set quota for a role that is about to be introduced.
  if (!master->roles.contains(role)) {
    return Error("Quota request for unknown role: '" + role + "'");
  }

  // Create QuotaInfo Protobuf.
  google::protobuf::RepeatedPtrField<Resource> resources =
    ::protobuf::parse<google::protobuf::RepeatedPtrField<Resource>>(parse.get())
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

  // Check that the request type is POST.
  CHECK_EQ(request.method, "POST");

  // Validate and convert the request to internal protobuf message.
  Try<QuotaInfo> validate = validateQuotaRequest(request);
  if (validate.isError()) {
    VLOG(1) << "Error validate quota request: "
            << validate.error();
    return BadRequest(validate.error());
  }

  const QuotaInfo& quotaInfo = validate.get();

  // Check we are not updating an existing Quota.
  // TODO(joerg84): Update error message once quota update is in place.
  if (master->quotas.contains(quotaInfo.role())) {
    return BadRequest(
        "Quota cannot be set for a role that already has quota");
  }

  // Check a quota is not set per role.

  // Validate whether a quota request can be satisfied.
  // TODO(alexr): Implement as per MESOS-3073.

  // Populated master's quota-related local state. We do it before updating the
  // registry in order to make sure that we are not already trying to satisfy a
  // reuqest for this role (since this is a multi-phase event).
  // NOTE: We do not need to remove quota for the role if the registry update
  // fails because in this case the master fails as well.
  Quota quota{quotaInfo};
  master->quotas[quotaInfo.role()] = quota;

  // Update registry with the new quota.
  // TODO(alexr): MESOS-3165.

  // We are all set, grant the request.
  // TODO(alexr): Implement as per MESOS-3073.
  // TODO(alexr): This should be done after registry operation succeeds.

  // Notfify allocator.
  master->allocator->setQuota(quotaInfo.role(), quotaInfo);

  return OK();
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
