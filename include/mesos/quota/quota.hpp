// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __MESOS_QUOTA_PROTO_HPP__
#define __MESOS_QUOTA_PROTO_HPP__

// ONLY USEFUL AFTER RUNNING PROTOC.
#include <mesos/quota/quota.pb.h>

#include <stout/error.hpp>
#include <stout/option.hpp>
#include <stout/try.hpp>

// A C++ wrapper for `QuotaInfo` used to communicate between the
// allocator and the master. Ensures contained `QuotaInfo` is valid.
class Quota
{
public:
  //
  static Try<Quota> create(const mesos::quota::QuotaInfo& info);

  //
  static Option<Error> validate(const mesos::quota::QuotaInfo& info);

  const mesos::quota::QuotaInfo& info() const;

private:
  // Because we want to validate `QuotaInfo` before constructing `Quota`
  // instance, we prohibit direct construction. However copy construction
  // and assignment are allowed.
  Quota(const mesos::quota::QuotaInfo& info);

  // Holds the quota protobuf, as constructed from an operator's request.
  mesos::quota::QuotaInfo info_;
};

#endif // __MESOS_QUOTA_PROTO_HPP__
