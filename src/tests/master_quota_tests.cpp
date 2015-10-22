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

#include <mesos/quota/quota.hpp>

#include <process/future.hpp>
#include <process/http.hpp>
#include <process/pid.hpp>

#include <stout/format.hpp>
#include <stout/strings.hpp>

#include "master/flags.hpp"
#include "master/master.hpp"

#include "slave/slave.hpp"

#include "tests/mesos.hpp"

using std::string;

using mesos::internal::master::Master;

using mesos::internal::slave::Slave;

using mesos::quota::QuotaInfo;

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


// Those of the overall quota tests that are allocator-agnostic (i.e. we expect
// every allocator to implement basic quota guarantees) are in this file. All
// tests are split into logical groups: request validation tests, tests for
// sanity check and registry, and so on.

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
    flags.roles = strings::join(",", role1, role2);
    return flags;
  }

  // Instructs agents to use specified amount of resources.
  virtual slave::Flags CreateSlaveFlags()
  {
    slave::Flags flags = MesosTest::CreateSlaveFlags();
    flags.resources = defaultAgentResourcesString;
    return flags;
  }

  process::http::Headers createBasicAuthHeaders(
      const Credential& credential) const
  {
    return process::http::Headers{{
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


// These are request validation tests. They verify JSON is well-formed,
// convertible to corresponding protobufs, all necessary fields are present,
// while irrelevant are not.

// TODO(alexr): Tests to implement:
//   * Malformed JSON (parsing fails).
//   * JSON is OK but could not be converted to QuotaInfo proto (merge with the
//     previous one)?
//   * Non-existent role.
//   * Role is absent.
//   * Resources with the same name are present.
//   * Irrelevant fields (reservation info, disk info, etc).

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

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(BadRequest().status, response)
    << response.get().body;

  Shutdown();
}


// These tests check whether a request makes sense in terms of current cluster
// status. A quota request may be well-formed, but obviously infeasible, e.g.
// request for 100 CPUs in a cluster with just 11 CPUs.

// TODO(alexr): Tests to implement:
//   * Sufficient total resources, but insufficient free resources due to
//     running tasks (multiple agents).
//   * Sufficient total resources, but insufficient free resources due to
//     dynamic reservations.

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
  EXPECT_EQ(defaultAgentResources, agentTotalResources.get());

  // Our quota request requires more resources than available on the agent (and
  // in the cluster).
  Resources quotaResources =
    agentTotalResources.get().filter([=](const Resource& resource) {
      return (resource.name() == "cpus" || resource.name() == "mem");
    }) +
    Resources::parse("cpus:1;mem:1024").get();

  quotaResources = quotaResources.flatten(role1);

  EXPECT_FALSE(agentTotalResources.get().contains(quotaResources.flatten()));

  Future<Response> response = process::http::post(
      master.get(),
      "quota",
      createBasicAuthHeaders(DEFAULT_CREDENTIAL),
      createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(Conflict().status, response)
    << response.get().body;

  Shutdown();
}


// Checks that a quota request is not satisfied if there are not enough
// resources.
TEST_F(MasterQuotaTest, InsufficientResourcesMultipleAgents)
{
  TestAllocator<> allocator;
  EXPECT_CALL(allocator, initialize(_, _, _, _));

  Try<PID<Master>> master = StartMaster(&allocator);
  ASSERT_SOME(master);

  // Start one agent and wait until it registers.
  Try<PID<Slave>> agent1 = StartSlave();
  ASSERT_SOME(agent1);

  Future<Resources> agent1TotalResources;
  EXPECT_CALL(allocator, addSlave(_, _, _, _, _))
    .WillOnce(DoAll(InvokeAddSlave(&allocator),
                   FutureArg<3>(&agent1TotalResources)));

  AWAIT_READY(agent1TotalResources);
  EXPECT_EQ(defaultAgentResources, agent1TotalResources.get());

  // Start another agent and wait until it registers.
  Try<PID<Slave>> agent2 = StartSlave();
  ASSERT_SOME(agent2);

  Future<Resources> agent2TotalResources;
  EXPECT_CALL(allocator, addSlave(_, _, _, _, _))
    .WillOnce(DoAll(InvokeAddSlave(&allocator),
                   FutureArg<3>(&agent2TotalResources)));

  AWAIT_READY(agent2TotalResources);
  EXPECT_EQ(defaultAgentResources, agent2TotalResources.get());

  // Our quota request requires more resources than available on the agent (and
  // in the cluster).
  Resources quotaResources =
    agent1TotalResources.get().filter([=](const Resource& resource) {
      return (resource.name() == "cpus" || resource.name() == "mem");
    }) +
    agent2TotalResources.get().filter([=](const Resource& resource) {
      return (resource.name() == "cpus" || resource.name() == "mem");
    }) +
    Resources::parse("cpus:1;mem:1024").get();

  quotaResources = quotaResources.flatten(role1);
  EXPECT_FALSE((agent1TotalResources.get() + agent2TotalResources.get())
                 .contains(quotaResources.flatten()));

  Future<Response> response = process::http::post(
      master.get(),
      "quota",
      createBasicAuthHeaders(DEFAULT_CREDENTIAL),
      createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(Conflict().status, response)
    << response.get().body;

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
  EXPECT_CALL(allocator, setQuota(Eq(role1), _))
    .WillOnce(DoAll(InvokeSetQuota(&allocator),
                    FutureArg<1>(&receivedQuotaRequest)));

  Future<Response> response = process::http::post(
      master.get(),
      "quota",
      createBasicAuthHeaders(DEFAULT_CREDENTIAL),
      createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response) << response.get().body;

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

  // Start one agent and wait until it registers.
  Try<PID<Slave>> agent1 = StartSlave();
  ASSERT_SOME(agent1);

  Future<Resources> agent1TotalResources;
  EXPECT_CALL(allocator, addSlave(_, _, _, _, _))
    .WillOnce(DoAll(InvokeAddSlave(&allocator),
                   FutureArg<3>(&agent1TotalResources)));
  AWAIT_READY(agent1TotalResources);
  EXPECT_EQ(defaultAgentResources, agent1TotalResources.get());

  // Start another agent and wait until it registers.
  Try<PID<Slave>> agent2 = StartSlave();
  ASSERT_SOME(agent2);

  Future<Resources> agent2TotalResources;
  EXPECT_CALL(allocator, addSlave(_, _, _, _, _))
    .WillOnce(DoAll(InvokeAddSlave(&allocator),
                   FutureArg<3>(&agent2TotalResources)));
  AWAIT_READY(agent2TotalResources);
  EXPECT_EQ(defaultAgentResources, agent2TotalResources.get());

  // We request quota for a portion of resources which is not available on a
  // single agent.
  Resources quotaResources =
    agent1TotalResources.get().filter([=](const Resource& resource) {
      return (resource.name() == "cpus" || resource.name() == "mem");
    }) +
    agent2TotalResources.get().filter([=](const Resource& resource) {
      return (resource.name() == "cpus" || resource.name() == "mem");
    });

  quotaResources = quotaResources.flatten(role1);

  // Send a quota request for the specified role.
  Future<QuotaInfo> receivedQuotaRequest;
  EXPECT_CALL(allocator, setQuota(Eq(role1), _))
    .WillOnce(DoAll(InvokeSetQuota(&allocator),
                    FutureArg<1>(&receivedQuotaRequest)));

  Future<Response> response = process::http::post(
      master.get(),
      "quota",
      createBasicAuthHeaders(DEFAULT_CREDENTIAL),
      createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response) << response.get().body;

  // Quota request is granted and reached the allocator. Make sure nothing got
  // lost in-between.
  AWAIT_READY(receivedQuotaRequest);

  EXPECT_EQ(role1, receivedQuotaRequest.get().role());
  EXPECT_EQ(quotaResources, Resources(receivedQuotaRequest.get().guarantee()));

  Shutdown();
}


// Test whether we can retrieve the current quota status from
// /master/quota endpoint via a GET request against /quota.
TEST_F(MasterQuotaTest, QuotaEndpointStatus)
{
  // Start up the master.
  Try<PID<Master>> master = StartMaster();
  ASSERT_SOME(master);

  // Start two agents each having cpus:2;mem:1024;disk:1024;ports:[31000-32000].
  Future<SlaveRegisteredMessage> agent1RegisteredMessage =
    FUTURE_PROTOBUF(SlaveRegisteredMessage(), master.get(), _);

  Try<PID<Slave>> agent1 = StartSlave();
  ASSERT_SOME(agent1);

  Future<SlaveRegisteredMessage> agent2RegisteredMessage = FUTURE_PROTOBUF(
    SlaveRegisteredMessage(), master.get(), testing::Not(agent1.get()));

  Try<PID<Slave>> agent2 = StartSlave();
  ASSERT_SOME(agent2);

  // Wait for the agent to be registered.
  AWAIT_READY(agent1RegisteredMessage);
  AWAIT_READY(agent2RegisteredMessage);

  // We request quota for a portion of resources available on the agents.
  Resources quotaResources =
    Resources::parse("cpus:4;mem:512", "quota_ads").get();

  // Send a quota request for the specified role.
  Future<Response> response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::Accepted().status, response);

  // Query the master quota endpoint.
  response = process::http::get(master.get(), "quota");
  AWAIT_READY(response);

  EXPECT_SOME_EQ(
      "application/json",
      response.get().headers.get("Content-Type"));

  const Try<JSON::Object> parse =
    JSON::parse<JSON::Object>(response.get().body);

  ASSERT_SOME(parse);

  // Check that there are at least two elements in the array.
  Result<JSON::Array> array = parse.get().find<JSON::Array>("slaves");
  ASSERT_SOME(array);
  EXPECT_EQ(2u, array.get().values.size());

  // TODO(joerg84) Add checks for quota

  Shutdown();
}


// Test whether we can request a new quota via /master/quota endpoint.
// Should return a '200 Ok' return Code.
TEST_F(MasterQuotaTest, QuotaEndpointCreate)
{
  // Start up the master.
  Try<PID<Master>> master = StartMaster();
  ASSERT_SOME(master);

  // We request quota for a portion of resources available on the agents.
  Resources quotaResources =
    Resources::parse("cpus:4;mem:512", "quota_ads").get();

  // Send a quota request for the specified role.
  Future<Response> response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::Accepted().status, response);

  Shutdown();
}


// Test whether a QuotaRequest with non-scalar resources fails.
// Should return a '400 Bad Request' return Code.
TEST_F(MasterQuotaTest, QuotaEndpointCreateNonScalar)
{
  // Start up the master.
  Try<PID<Master>> master = StartMaster();
  ASSERT_SOME(master);

  // We request quota for a portion of resources available on the agents.
  Resources quotaResources =
    Resources::parse(
        "cpus:4;mem:512;ports:[10000-20000, 30000-50000]", "quota_ads").get();

  // Send a quota request for the specified role.
  Future<Response> response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::BadRequest().status, response);

  Shutdown();
}


// Test whether a quota request withinvalid json.
// Should return a '400 Bad Request' return code.
TEST_F(MasterQuotaTest, QuotaEndpointCreateInvalid)
{
  // Start up the master.
  Try<PID<Master>> master = StartMaster();
  ASSERT_SOME(master);

  string badRequest = "{"
      "invalidJson"
      "}";

  Future<process::http::Response> response = process::http::post(
      master.get(),
      "quota",
      None(),
      Option<std::string>(badRequest),
      None());

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::BadRequest().status, response);

  Shutdown();
}


// Test whether an quota request including multiple roles
// returns a '400 Bad Request'.
TEST_F(MasterQuotaTest, QuotaEndpointCreateInvalidRoles)
{
  // Start up the master.
  Try<PID<Master>> master = StartMaster();
  ASSERT_SOME(master);

  // We request quota for two different roles.
  Resources quotaResources =
    Resources::parse(
        "cpus:4;mem:512;ports:[10000-20000, 30000-50000]", "quota_ads").get();

  Resources quotaResources2 =
    Resources::parse(
        "cpus:4;mem:512;ports:[10000-20000, 30000-50000]", "quota_ads2").get();

  quotaResources += quotaResources2;

  // Send a quota request for the specified role.
  Future<Response> response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::BadRequest().status, response);

  Shutdown();
}


// Test whether updating an exiting quota for an via via POST to
// /master/quota endpoint results in an error (as this should be done
// via PUT to /master/quota/role endpoint). Should return a
// '409 Existing Resource' return Code.
TEST_F(MasterQuotaTest, QuotaEndpointCreateExisting)
{
  // Start up the master.
  Try<PID<Master>> master = StartMaster();
  ASSERT_SOME(master);

  // We request quota for a portion of resources available on the agents.
  Resources quotaResources =
    Resources::parse("cpus:4;mem:512", "quota_ads").get();

  // Send a quota request for the specified role.
  Future<Response> response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::Accepted().status, response);

  // Incremental quota request via post.
  response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::BadRequest().status, response);

  Shutdown();
}


// Check whether a quota request with either invalid field set is is rejected:
// - ReservationInfo
// - RevocableInfo
// - DiskInfo
TEST_F(MasterQuotaTest, InvalidResourceInfos)
{
  // Start up the master.
  Try<PID<Master>> master = StartMaster();
  ASSERT_SOME(master);

  // Create Resources with DiskInfo and check request will return a BadRequest.
  Resources quotaResources =
    Resources::parse("cpus:4;mem:512", "quota_ads").get();
  Resource volume = Resources::parse("disk", "128", "role1").get();
  volume.mutable_disk()->CopyFrom(createDiskInfo("id1", "path1"));
  quotaResources += volume;
  // Send a quota request for the specified role.
  Future<Response> response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));
  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::BadRequest().status, response);

  // Create Resources with RevocableInfo and check request will return
  // a BadRequest.
  quotaResources =
    Resources::parse("cpus:4;mem:512", "quota_ads").get();
  volume = Resources::parse("disk", "128", "role1").get();
  volume.mutable_revocable();
  quotaResources += volume;
  // Send a quota request.
  response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));
  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::BadRequest().status, response);

  // Create Resources with ReservationInfo and check request will return
  // a BadRequest.
  quotaResources =
    Resources::parse("cpus:4;mem:512", "quota_ads").get();
  volume = Resources::parse("disk", "128", "role1").get();
  volume.mutable_reservation()->CopyFrom(
      createReservationInfo("principal"));
  quotaResources += volume;
  // Send a quota request.
  response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));
  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::BadRequest().status, response);
}


// Test to release existing quota. Should return a '200 Ok'
// return code.
TEST_F(MasterQuotaTest, QuotaEndpointRelease)
{
  // Start up the master.
  Try<PID<Master>> master = StartMaster();
  ASSERT_SOME(master);

  // Initial quota request.
  Resources quotaResources =
    Resources::parse("cpus:4;mem:512", "quota_ads").get();

  // Send a quota request for the specified role.
  Future<Response> response = process::http::post(
        master.get(),
        "quota",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL),
        createRequestBody(quotaResources));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::Accepted().status, response);

  // Send a delete request for the 'quota_ads' role.
  response = process::http::requestDelete(
        master.get(),
        "quota/quota_ads",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::Accepted().status, response);

  Shutdown();
}


// Test to release non-existing quota. Should return a '409 Conflict'
// return code.
TEST_F(MasterQuotaTest, QuotaEndpointInvalidRelase)
{
  // Start up the master.
  Try<PID<Master>> master = StartMaster();
  ASSERT_SOME(master);

  // Send a delete request for the 'quota_ads' role.
  Future<Response> response = process::http::requestDelete(
        master.get(),
        "quota/non-existent-role",
        createBasicAuthHeaders(DEFAULT_CREDENTIAL));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(process::http::BadRequest().status, response);

  Shutdown();
}

// These tests ensure quota implements declared functionality. Note that the
// tests here are allocator-agnostic, which means we expect every allocator to
// implement basic quota guarantees.

// TODO(alexr): Tests to implement:
//   * An agent with quota'ed tasks disconnects and there are not enough free
//     resources (alert and under quota situation).
//   * An agent with quota'ed tasks disconnects and there are enough free
//     resources (new offers).
//   * Role quota is below its allocation (InverseOffer generation).
//   * Two roles, two frameworks, one is production but rejects offers, the
//     other is greedy and tries to hijack the cluster which is prevented by
//     quota.
//   * Quota'ed and non-quota'ed roles, multiple frameworks in quota'ed role,
//     ensure total allocation sums up to quota.
//   * Remove quota with no running tasks.
//   * Remove quota with running tasks.


// These tests verify the behavior in presence of master failover and recovery.

// TODO(alexr): Tests to implement:
//   * During the recovery, no overcommitment of resources should happen.
//   * During the recovery, no allocation of resources potentially needed to
//     satisfy quota should happen.
//   * If a cluster is under quota before the failover, it should be under quota
//     during the recovery (total quota sanity check).
//   * Master fails simultaneously with multiple agents, rendering the cluster
//     under quota (total quota sanity check).


// These are registry tests.

} // namespace tests {
} // namespace internal {
} // namespace mesos {
