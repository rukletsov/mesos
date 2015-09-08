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
 * limitations under the License.
 */

#ifndef __MASTER_QUOTA_HPP__
#define __MASTER_QUOTA_HPP__

#include <mesos/quota/quota.hpp>

#include "master/registrar.hpp"
#include "master/registry.hpp"

namespace mesos {
namespace internal {
namespace master {
namespace quota {

class AddQuota : public Operation
{
public:
  explicit AddQuota(
      const mesos::quota::QuotaInfo& _quota);

protected:
  Try<bool> perform(
      Registry* registry,
      hashset<SlaveID>* slaveIDs,
      bool strict);

private:
  const mesos::quota::QuotaInfo quota;
};


class RemoveQuota : public Operation
{
public:
  explicit RemoveQuota(
      const mesos::quota::QuotaInfo& _quota);

protected:
  Try<bool> perform(
      Registry* registry,
      hashset<SlaveID>* slaveIDs,
      bool strict);

private:
  const mesos::quota::QuotaInfo quota;
};

} // namepsace quota {
} // namespace master {
} // namespace internal {
} // namespace mesos {

#endif // __MASTER_QUOTA_HPP__
