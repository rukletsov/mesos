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

#ifndef __MASTER_QUOTA_HANDLER_HPP__
#define __MASTER_QUOTA_HANDLER_HPP__

#include <process/future.hpp>
#include <process/http.hpp>

namespace mesos {
namespace internal {

// Handles quota inside the master actor and hence is responsible for
// validating and persisting quota requests; and exposing quota status.
class QuotaHandler
{
public:
  // TODO(joerg84): As of right now this is a stub and will be filled
  // as part of MESOS-1791.

  process::Future<process::http::Response> request(
      const process::http::Request& request)
  {
    return process::http::Accepted();
  }

  process::Future<process::http::Response> release(
      const process::http::Request& request)
  {
    return process::http::Accepted();
  }

  process::Future<process::http::Response> status(
      const process::http::Request& request)
  {
    return process::http::Accepted();
  }
};

} // namespace internal {
} // namespace mesos {

#endif // __MASTER_QUOTA_HANDLER_HPP__
