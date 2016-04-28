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

#include <string>
#include <vector>

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
using std::vector;

using testing::DoAll;

namespace mesos {
namespace internal {
namespace tests {

// Returns a list of endpoints we run the tests against.
static vector<string> ENDPOINTS()
{
  return {"monitor/statistics", "monitor/statistics.json", "flags"};
}


// Causes all TYPED_TEST(SlaveAuthorizationTest, ...) to be run for each
// of the specified Authorizer classes. Currently, we also run each test
// for each endpoint that supports coarse-grained authorization. This
// seems redundant, because in order to ensure all components (agent
// endpoint handlers, authorizer, ACLs) are configured correctly, we need
// to run this test suite *once* against _any_ suitable endpoint. In
// order to check whether an endpoint supports coarse-grained authorization
// (or, more precisely, forwards authorization requests to the authorizer
// in an expected way), we do not need to write an integration test and
// instantiate a real authorizer, e.g. see `SlaveEndpointTest` fixture.
//
// TODO(alexr): Split responsibilities between `SlaveAuthorizationTest`
// and SlaveEndpointTest`. We want to test that:
//   * _each_ endpoint reacts correctly to certain authorization requests
//     (authorizer can be mocked in this case);
//   * the whole pipeline (endpoint handlers, authorizer, ACLs) works for
//     _any_ of such endpoints.
//
// NOTE: Ideally, we would also parameterize this test fixture by endpoint
// being queried. Unfortunately, gtest does not allow to parametrize a test
// fixture by both type and value. Hence we have to do it manually.
template <typename T>
class SlaveAuthorizationTest : public MesosTest {};


typedef ::testing::Types<
  LocalAuthorizer,
  tests::Module<Authorizer, TestLocalAuthorizer>>
  AuthorizerTypes;


TYPED_TEST_CASE(SlaveAuthorizationTest, AuthorizerTypes);


// This test verifies that only authorized principals
// can access the specified endpoint.
TYPED_TEST(SlaveAuthorizationTest, AuthorizeEndpoint)
{
  auto testCaseBody = [this](const string& endpoint) {
    // Setup ACLs so that only the default principal
    // can access the specified endpoint.
    ACLs acls;
    acls.set_permissive(false);

    mesos::ACL::GetEndpoint* acl = acls.add_get_endpoints();
    acl->mutable_principals()->add_values(DEFAULT_CREDENTIAL.principal());
    acl->mutable_paths()->add_values("/" + endpoint);

    // Create an `Authorizer` with the ACLs.
    Try<Authorizer*> create = TypeParam::create(parameterize(acls));
    ASSERT_SOME(create);
    Owned<Authorizer> authorizer(create.get());

    StandaloneMasterDetector detector;
    Try<Owned<cluster::Slave>> agent =
      this->StartSlave(&detector, authorizer.get());
    ASSERT_SOME(agent);

    Future<Response> response = http::get(
        agent.get()->pid,
        endpoint,
        None(),
        createBasicAuthHeaders(DEFAULT_CREDENTIAL));

    AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response)
      << response.get().body;

    response = http::get(
        agent.get()->pid,
        endpoint,
        None(),
        createBasicAuthHeaders(DEFAULT_CREDENTIAL_2));

    AWAIT_EXPECT_RESPONSE_STATUS_EQ(Forbidden().status, response)
      << response.get().body;
  };

  foreach (const string& endpoint, ENDPOINTS()) {
    testCaseBody(endpoint);
  }
}


// This test verifies that access to the specified endpoint can be authorized
// without authentication if an authorization rule exists that applies to
// anyone. The authorizer will map the absence of a principal to "ANY".
TYPED_TEST(SlaveAuthorizationTest, AuthorizeEndpointWithoutPrincipal)
{
  auto testCaseBody = [this](const string& endpoint) {
    // Because the authenticators' lifetime is tied to libprocess's lifetime,
    // it may have already been set by other tests. We have to unset it here
    // to disable HTTP authentication.
    //
    // TODO(nfnt): Fix this behavior. The authenticator should be unset by
    // every test case that sets it, similar to how it's done for the master.
    http::authentication::unsetAuthenticator(
        slave::DEFAULT_HTTP_AUTHENTICATION_REALM);

    // Setup ACLs so that any principal can access the specified endpoint.
    ACLs acls;
    acls.set_permissive(false);

    mesos::ACL::GetEndpoint* acl = acls.add_get_endpoints();
    acl->mutable_principals()->set_type(mesos::ACL::Entity::ANY);
    acl->mutable_paths()->add_values("/" + endpoint);

    slave::Flags agentFlags = this->CreateSlaveFlags();
    agentFlags.acls = acls;
    agentFlags.authenticate_http = false;
    agentFlags.http_credentials = None();

    // Create an `Authorizer` with the ACLs.
    Try<Authorizer*> create = TypeParam::create(parameterize(acls));
    ASSERT_SOME(create);
    Owned<Authorizer> authorizer(create.get());

    StandaloneMasterDetector detector;
    Try<Owned<cluster::Slave>> agent = this->StartSlave(
        &detector, authorizer.get(), agentFlags);
    ASSERT_SOME(agent);

    Future<Response> response = http::get(agent.get()->pid, endpoint);

    AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response)
      << response.get().body;
  };

  foreach (const string& endpoint, ENDPOINTS()) {
    testCaseBody(endpoint);
  }
}


// Parameterized fixture for agent-specific authorization tests. The
// path of the tested endpoint is passed as the only parameter.
class SlaveEndpointTest:
    public MesosTest,
    public ::testing::WithParamInterface<string> {};


// The tests are parameterized by the endpoint being queried.
// See `ENDPOINTS()` for the list of target endpoints.
//
// TODO(bbannier): Once agent endpoint handlers use more than just
// `GET_ENDPOINT_WITH_PATH`, we should consider parameterizing
// `SlaveEndpointTest` by the authorization action as well.
INSTANTIATE_TEST_CASE_P(
    Endpoint,
    SlaveEndpointTest,
    ::testing::ValuesIn(ENDPOINTS()));


// Tests that an agent endpoint handler form
// correct queries against the authorizer.
TEST_P(SlaveEndpointTest, AuthorizedRequest)
{
  const string endpoint = GetParam();

  StandaloneMasterDetector detector;

  MockAuthorizer mockAuthorizer;

  Try<Owned<cluster::Slave>> agent = StartSlave(&detector, &mockAuthorizer);
  ASSERT_SOME(agent);

  Future<authorization::Request> request;
  EXPECT_CALL(mockAuthorizer, authorized(_))
    .WillOnce(DoAll(FutureArg<0>(&request),
                    Return(true)));

  Future<Response> response = http::get(
      agent.get()->pid,
      endpoint,
      None(),
      createBasicAuthHeaders(DEFAULT_CREDENTIAL));

  AWAIT_READY(request);

  const string principal = DEFAULT_CREDENTIAL.principal();
  EXPECT_EQ(principal, request.get().subject().value());

  // TODO(bbannier): Once agent endpoint handlers use more than just
  // `GET_ENDPOINT_WITH_PATH` we should factor out the request method
  // and expected authorization action and parameterize
  // `SlaveEndpointTest` on that as well in addition to the endpoint.
  EXPECT_EQ(authorization::GET_ENDPOINT_WITH_PATH, request.get().action());

  EXPECT_EQ("/" + endpoint, request.get().object().value());

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response)
    << response.get().body;
}


// Tests that unauthorized requests for an agent endpoint are properly rejected.
TEST_P(SlaveEndpointTest, UnauthorizedRequest)
{
  const string endpoint = GetParam();

  StandaloneMasterDetector detector;

  MockAuthorizer mockAuthorizer;

  Try<Owned<cluster::Slave>> agent = StartSlave(&detector, &mockAuthorizer);
  ASSERT_SOME(agent);

  EXPECT_CALL(mockAuthorizer, authorized(_))
    .WillOnce(Return(false));

  Future<Response> response = http::get(
      agent.get()->pid,
      endpoint,
      None(),
      createBasicAuthHeaders(DEFAULT_CREDENTIAL));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(Forbidden().status, response)
    << response.get().body;
}


// Tests that requests for an agent endpoint
// always succeed if the authorizer is absent.
TEST_P(SlaveEndpointTest, NoAuthorizer)
{
  const string endpoint = GetParam();

  StandaloneMasterDetector detector;

  Try<Owned<cluster::Slave>> agent = StartSlave(&detector, CreateSlaveFlags());
  ASSERT_SOME(agent);

  Future<Response> response = http::get(
      agent.get()->pid,
      endpoint,
      None(),
      createBasicAuthHeaders(DEFAULT_CREDENTIAL));

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response)
    << response.get().body;
}

} // namespace tests {
} // namespace internal {
} // namespace mesos {
