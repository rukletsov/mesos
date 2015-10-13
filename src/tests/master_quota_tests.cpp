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

#include <gmock/gmock.h>

#include <string>

#include <mesos/mesos.hpp>
#include <mesos/resources.hpp>

#include <mesos/master/quota.hpp>

#include <process/future.hpp>
#include <process/http.hpp>
#include <process/pid.hpp>

#include <stout/format.hpp>

#include "master/flags.hpp"
#include "master/master.hpp"

#include "slave/slave.hpp"

#include "tests/mesos.hpp"

using std::string;

using mesos::internal::master::Master;
using mesos::internal::slave::Slave;

using mesos::master::QuotaInfo;

using process::Future;
using process::PID;

using process::http::BadRequest;
using process::http::Conflict;
using process::http::OK;
using process::http::Response;

using testing::_;
using testing::DoAll;
using testing::Eq;

namespace mesos {
namespace internal {
namespace tests {

// Converts a 'RepeatedPtrField<Resource>' to a 'JSON::Array'.
// TODO(alexr): Replace once MESOS-3405 lands.
static JSON::Array toJSONArray(
    const google::protobuf::RepeatedPtrField<Resource>& resources)
{
  JSON::Array array;

  array.values.reserve(resources.size());

  foreach (const Resource& resource, resources) {
    array.values.push_back(JSON::Protobuf(resource));
  }

  return array;
}


// TODO(alexr): mention what kind of tests (non-allocator specific, but some
// general integration tests included.
// TODO(alexr): Once we have more allocators, convert this test into a typed one
// over multiple allocators.
class MasterQuotaTest : public MesosTest
{
protected:
  MasterQuotaTest() : nextFrameworkId(1)
  {
    defaultAgentResources = Resources::parse(defaultAgentResourcesString).get();
  }

  // Sets up the master flags with two roles and tiny allocation interval.
  virtual master::Flags CreateMasterFlags()
  {
    master::Flags flags = MesosTest::CreateMasterFlags();
    flags.allocation_interval = Milliseconds(50);
    flags.roles = role1 + "," + role2;
    return flags;
  }

  // Sets
  virtual slave::Flags CreateSlaveFlags()
  {
    slave::Flags flags = this->CreateSlaveFlags();
    flags.resources = defaultAgentResourcesString;
    return flags;
  }

  hashmap<string, string> createBasicAuthHeaders(
      const Credential& credential) const
  {
    return hashmap<string, string>{{
      "Authorization",
      "Basic " +
        base64::encode(credential.principal() + ":" + credential.secret())
    }};
  }

  // Creates a FrameworkInfo with a specified role.
  FrameworkInfo createFrameworkInfo(const string& role)
  {
    FrameworkInfo info;
    info.set_user("user");
    info.set_name("framework" + stringify(nextFrameworkId++));
    info.mutable_id()->set_value(info.name());
    info.set_role(role);

    return info;
  }

  // Generates a quota request from provided resources.
  string createRequestBody(const Resources& resources) const
  {
    return strings::format("resources=%s", toJSONArray(resources)).get();
  }

protected:
  const std::string role1{"role1"};
  const std::string role2{"role2"};

  const std::string defaultAgentResourcesString{
    "cpus:2;mem:1024;disk:1024;ports:[31000-32000]"};
  Resources defaultAgentResources;

private:
  int nextFrameworkId;
};


// TODO(alexr): What tests do we want here:
//   * Registry tests (have a look at maintenance registry tests);
//   * Master failover in presense of quota (and whether overcommitment of
//     resources occur);
//   * Satisfiability tests;
//   * Two roles, two frameworks, one rejects the first offer, the other is
//     greedy and hijacks the cluster; no quota
//   * Same as previous, but with the quota set;
//   * A single framework in the role, role quota is decreased below its
//     allocation (InverseOffer generation);
//   * An agent with quota-backed tasks disconnects;


// Verifies that a request for a non-existent role is rejected.
// TODO(alexr): This may be revisited once we allow dynamic roles and therefore
// assigning quota before a role is known to the master.
TEST_F(MasterQuotaTest, NonExistentRole)
{
  Try<PID<Master>> master = StartMaster();
  ASSERT_SOME(master);

  // We do not need an agent since a request should be rejected before we start
  // looking at available resources.

  // We request quota for a portion of resources available on the agent.
  Resources quotaResources =
    Resources::parse("cpus:1;mem:512", "non-existent-role").get();

  // Send a quota request for the specified role.
  Future<Response> response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(BadRequest().status, response);

  Shutdown();
}


// Checks that a quota request is not satisfied if there are not enough
// resources.
TEST_F(MasterQuotaTest, InsufficientResourcesSingleAgent)
{
  TestAllocator<> allocator;

  EXPECT_CALL(allocator, initialize(_, _, _, _));

  Try<PID<Master>> master = StartMaster(&allocator);
  ASSERT_SOME(master);

  Try<PID<Slave>> agent = StartSlave();
  ASSERT_SOME(agent);

  // Wait until the agent registers.
  Future<Resources> agentTotalResources;
  EXPECT_CALL(allocator, addSlave(_, _, _, _, _))
    .WillOnce(DoAll(InvokeAddSlave(&allocator),
                   FutureArg<3>(&agentTotalResources)));
  AWAIT_READY(agentTotalResources);

  // Our quota request requires more resources than available on the agent (and
  // in the cluster).
  Resources quotaResources =
    Resources::parse("cpus:3;mem:1024", role1).get();
  EXPECT_EQ(defaultAgentResources, agentTotalResources.get());
  EXPECT_FALSE(agentTotalResources.get().contains(quotaResources.flatten()));

  Future<Response> response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(Conflict().status, response);

  Shutdown();
}


// Checks that an operator can request quota when enough resources are
// available on single agent.
TEST_F(MasterQuotaTest, AvailableResourcesSingleAgent)
{
  TestAllocator<> allocator;

  EXPECT_CALL(allocator, initialize(_, _, _, _));

  Try<PID<Master>> master = StartMaster(&allocator);
  ASSERT_SOME(master);

  Try<PID<Slave>> agent = StartSlave();
  ASSERT_SOME(agent);

  // Wait until the agent registers.
  Future<Resources> agentTotalResources;
  EXPECT_CALL(allocator, addSlave(_, _, _, _, _))
    .WillOnce(DoAll(InvokeAddSlave(&allocator),
                   FutureArg<3>(&agentTotalResources)));
  AWAIT_READY(agentTotalResources);

  // We request quota for a portion of resources available on the agent.
  Resources quotaResources =
    Resources::parse("cpus:1;mem:512", role1).get();
  EXPECT_EQ(defaultAgentResources, agentTotalResources.get());
  EXPECT_TRUE(agentTotalResources.get().contains(quotaResources.flatten()));

  // Send a quota request for the specified role.
  Future<QuotaInfo> receivedQuotaRequest;
  EXPECT_CALL(allocator, addQuota(Eq(role1), _))
    .WillOnce(DoAll(InvokeAddQuota(&allocator),
                    FutureArg<1>(&receivedQuotaRequest)));

  Future<Response> response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response);

  // Quota request is granted and reached the allocator. Make sure nothing got
  // lost in-between.
  AWAIT_READY(receivedQuotaRequest);

  EXPECT_EQ(role1, receivedQuotaRequest.get().role());
  EXPECT_EQ(quotaResources, Resources(receivedQuotaRequest.get().guarantee()));

  Shutdown();
}


// Checks that an operator can request quota when enough resources are
// available in the cluster, but not on a single agent.
TEST_F(MasterQuotaTest, AvailableResourcesMultipleAgents)
{
  TestAllocator<> allocator;

  EXPECT_CALL(allocator, initialize(_, _, _, _));

  Try<PID<Master>> master = StartMaster(&allocator);
  ASSERT_SOME(master);

  Try<PID<Slave>> agent1 = StartSlave();
  ASSERT_SOME(agent1);

  Try<PID<Slave>> agent2 = StartSlave();
  ASSERT_SOME(agent2);

  // Wait until the agents register.
  Future<Resources> agent1TotalResources;
  EXPECT_CALL(allocator, addSlave(_, _, _, _, _))
    .WillOnce(DoAll(InvokeAddSlave(&allocator),
                   FutureArg<3>(&agent1TotalResources)));
  AWAIT_READY(agent1TotalResources);
  EXPECT_EQ(defaultAgentResources, agent1TotalResources.get());

  Future<Resources> agent2TotalResources;
  EXPECT_CALL(allocator, addSlave(_, _, _, _, _))
    .WillOnce(DoAll(InvokeAddSlave(&allocator),
                   FutureArg<3>(&agent2TotalResources)));
  AWAIT_READY(agent2TotalResources);
  EXPECT_EQ(defaultAgentResources, agent2TotalResources.get());

  // We request quota for a portion of resources which is not available on a
  // single agent.
  Resources quotaResources =
    (agent1TotalResources.get() + agent2TotalResources.get()).flatten(role1);

  // Send a quota request for the specified role.
  Future<QuotaInfo> receivedQuotaRequest;
  EXPECT_CALL(allocator, addQuota(Eq(role1), _))
    .WillOnce(DoAll(InvokeAddQuota(&allocator),
                    FutureArg<1>(&receivedQuotaRequest)));

  Future<Response> response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response);

  // Quota request is granted and reached the allocator. Make sure nothing got
  // lost in-between.
  AWAIT_READY(receivedQuotaRequest);

  EXPECT_EQ(role1, receivedQuotaRequest.get().role());
  EXPECT_EQ(quotaResources, Resources(receivedQuotaRequest.get().guarantee()));

  Shutdown();
}

} // namespace tests {
} // namespace internal {
} // namespace mesos {
