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

  // Check there are no multiple resources of the same name, Check irrelevant
  // fields are not set: reservatin, disk, etc.

  // TODO(alexr): Once we are able to dynamically add roles, we should stop
  // checking whether the requested role is known to the master, because an
  // operator may set quota for a role that is about to be introduced.

  // TODO(alexr): This code creates a `QuotaInfo` protobuf message in a
  // straightforward and unsafe manner to enable testing. It lacks validation
  // which is added in subsequent patches.
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


Future<Response> Master::QuotaHandler::set(const Request& request) const
{
  // Authenticate & authorize the request.
  // TODO(alexr): Check Master::Http::authenticate() for an example.

  // Next, validate and convert the request to internal protobuf message.
  Try<QuotaInfo> quotaInfo = validateQuotaRequest(request);
  if (quotaInfo.isError()) {
    return BadRequest(quotaInfo.error());
  }

  // Check a quota is not set per role.

  // Validate whether a quota request can be satisfied.
  // TODO(alexr): Implement as per MESOS-3073.

  // Populated master's quota-related local state. We do it before updating the
  // registry in order to make sure that we are not already trying to satisfy a
  // reuqest for this role (since this is a multi-phase event).
  // NOTE: We do not need to remove quota for the role if the registry update
  // fails because in this case the master fails as well.
  Quota quota{quotaInfo.get()};
  master->quotas[quotaInfo.get().role()] = quota;

  // Update registry with the new quota.
  // TODO(alexr): MESOS-3165.

  // We are all set, grant the request.
  // TODO(alexr): Implement as per MESOS-3073.
  // TODO(alexr): This should be done after registry operation succeeds.

  // Notfify allocator.
  master->allocator->setQuota(quotaInfo.get().role(), quotaInfo.get());

  return OK();
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
