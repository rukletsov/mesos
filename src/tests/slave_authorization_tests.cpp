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
// Unless required by applicable law or agreed to in writiDng, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>

#include <gmock/gmock.h>

#include <gtest/gtest.h>

#include <mesos/authorizer/authorizer.hpp>

#include <mesos/module/authorizer.hpp>

#include <process/http.hpp>
#include <process/owned.hpp>

#include <stout/try.hpp>

#include "authorizer/local/authorizer.hpp"

#include "master/detector/standalone.hpp"

#include "tests/mesos.hpp"
#include "tests/module.hpp"

namespace http = process::http;

using mesos::internal::master::Master;

using mesos::internal::slave::Slave;

using mesos::master::detector::StandaloneMasterDetector;

using process::Future;
using process::Owned;

using process::http::Forbidden;
using process::http::OK;
using process::http::Response;

using std::string;

using testing::DoAll;

namespace mesos {
namespace internal {
namespace tests {

static Parameters parameterize(const ACLs& acls)
{
  Parameters parameters;
  Parameter* parameter = parameters.add_parameter();
  parameter->set_key("acls");
  parameter->set_value(string(jsonify(JSON::Protobuf(acls))));

  return parameters;
}


template <typename T>
class SlaveAuthorizationTest : public MesosTest {};


typedef ::testing::Types<
  LocalAuthorizer,
  tests::Module<Authorizer, TestLocalAuthorizer>>
  AuthorizerTypes;


TYPED_TEST_CASE(SlaveAuthorizationTest, AuthorizerTypes);


// This test verifies that only authorized principals can access the
// '/flags' endpoint.
TYPED_TEST(SlaveAuthorizationTest, AuthorizeFlagsEndpoint)
{
  // Setup ACLs so that only the default principal can access the '/flags'
  // endpoint.
  ACLs acls;
  acls.set_permissive(false);

  mesos::ACL::GetEndpoint* acl = acls.add_get_endpoints();
  acl->mutable_principals()->add_values(DEFAULT_CREDENTIAL.principal());
  acl->mutable_paths()->add_values("/flags");

  // Create an `Authorizer` with the ACLs.
  Try<Authorizer*> create = TypeParam::create(parameterize(acls));
  ASSERT_SOME(create);
  Owned<Authorizer> authorizer(create.get());

  StandaloneMasterDetector detector;
  Try<Owned<cluster::Slave>> slave =
    this->StartSlave(&detector, authorizer.get());
  ASSERT_SOME(slave);

  Future<Response> response = http::get(
      slave.get()->pid,
      "flags",
      None(),
      createBasicAuthHeaders(DEFAULT_CREDENTIAL));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response)
    << response.get().body;

  response = http::get(
      slave.get()->pid,
      "flags",
      None(),
      createBasicAuthHeaders(DEFAULT_CREDENTIAL_2));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(Forbidden().status, response)
    << response.get().body;
}


// This test verifies that access to the '/flags' endpoint can be authorized
// without authentication if an authorization rule exists that applies to
// anyone. The authorizer will map the absence of a principal to "ANY".
TYPED_TEST(SlaveAuthorizationTest, AuthorizeFlagsEndpointWithoutPrincipal)
{
  // Because the authenticators' lifetime is tied to libprocess's lifetime,
  // it may already be set by other tests. We have to unset it here to disable
  // HTTP authentication.
  // TODO(nfnt): Fix this behavior. The authenticator should be unset by
  // every test case that sets it, similar to how it's done for the master.
  http::authentication::unsetAuthenticator(
      slave::DEFAULT_HTTP_AUTHENTICATION_REALM);

  // Setup ACLs so that any principal can access the '/flags' endpoint.
  ACLs acls;
  acls.set_permissive(false);

  mesos::ACL::GetEndpoint* acl = acls.add_get_endpoints();
  acl->mutable_principals()->set_type(mesos::ACL::Entity::ANY);
  acl->mutable_paths()->add_values("/flags");

  slave::Flags slaveFlags = this->CreateSlaveFlags();
  slaveFlags.acls = acls;
  slaveFlags.authenticate_http = false;
  slaveFlags.http_credentials = None();

  // Create an `Authorizer` with the ACLs.
  Try<Authorizer*> create =
    TypeParam::create(parameterize(slaveFlags.acls.get()));
  ASSERT_SOME(create);
  Owned<Authorizer> authorizer(create.get());

  StandaloneMasterDetector detector;
  Try<Owned<cluster::Slave>> slave = this->StartSlave(
      &detector,
      authorizer.get(),
      slaveFlags);
  ASSERT_SOME(slave);

  Future<Response> response = http::get(slave.get()->pid, "flags");

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response)
    << response.get().body;
}


class SlaveEndpointAuthorizationTest : public MesosTest {};


// This test checks that an agent's statistics endpoint is authorized.
TEST_F(SlaveEndpointAuthorizationTest, Statistics)
{
  StandaloneMasterDetector detector;

  const string statisticsEndpoints[] =
    {"monitor/statistics", "monitor/statistics.json"};

  foreach (const string& statisticsEndpoint, statisticsEndpoints) {
    // Test that the endpoint handler forms correct queries against
    // the authorizer.
    {
      MockAuthorizer mockAuthorizer;

      Try<Owned<cluster::Slave>> agent = StartSlave(&detector, &mockAuthorizer);
      ASSERT_SOME(agent);

      Future<authorization::Request> request;
      EXPECT_CALL(mockAuthorizer, authorized(_))
        .WillOnce(DoAll(FutureArg<0>(&request), Return(true)));

      Future<Response> response = process::http::get(
          agent.get()->pid,
          statisticsEndpoint,
          None(),
          createBasicAuthHeaders(DEFAULT_CREDENTIAL));

      AWAIT_READY(request);

      const string principal = DEFAULT_CREDENTIAL.principal();
      EXPECT_EQ(principal, request.get().subject().value());

      EXPECT_EQ(authorization::GET_ENDPOINT_WITH_PATH, request.get().action());

      EXPECT_EQ("/" + statisticsEndpoint, request.get().object().value());

      AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response)
          << response.get().body;
    }

    // Test that unauthorized requests are properly rejected.
    {
      MockAuthorizer mockAuthorizer;

      Try<Owned<cluster::Slave>> agent = StartSlave(&detector, &mockAuthorizer);
      ASSERT_SOME(agent);

      EXPECT_CALL(mockAuthorizer, authorized(_))
        .WillOnce(Return(false));

      Future<Response> response = process::http::get(
          agent.get()->pid,
          statisticsEndpoint,
          None(),
          createBasicAuthHeaders(DEFAULT_CREDENTIAL));

      AWAIT_EXPECT_RESPONSE_STATUS_EQ(Forbidden().status, response)
          << response.get().body;
    }

    // Test that without an active authorizer authorizations always succeed.
    {
      Try<Owned<cluster::Slave>> agent =
        cluster::Slave::start(&detector, CreateSlaveFlags());
      ASSERT_SOME(agent);

      Future<Response> response = process::http::get(
          agent.get()->pid,
          statisticsEndpoint,
          None(),
          createBasicAuthHeaders(DEFAULT_CREDENTIAL));

      AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response)
          << response.get().body;
    }
  }
}

} // namespace tests {
} // namespace internal {
} // namespace mesos {
