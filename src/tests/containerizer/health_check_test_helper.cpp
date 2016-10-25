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

#include <ostream>

#include <process/gtest.hpp>
#include <process/id.hpp>
#include <process/process.hpp>

#include <stout/os.hpp>

#include "tests/containerizer/health_check_test_helper.hpp"

using std::cerr;
using std::endl;

using process::Process;
using process::READONLY_HTTP_AUTHENTICATION_REALM;
using process::READWRITE_HTTP_AUTHENTICATION_REALM;

namespace mesos {
namespace internal {
namespace tests {

const char HealthCheckTestHelper::NAME[] = "HealthCheck";


class HttpServer : public Process<HttpServer>
{
public:
  HttpServer()
    : ProcessBase(process::ID::generate("http-server")) {}
};


HealthCheckTestHelper::Flags::Flags()
{
  add(&Flags::ip,
      "ip",
      "IP address to listen on.");

  add(&Flags::port,
      "port",
      "Port to listen on.");
}


int HealthCheckTestHelper::execute()
{
  os::setenv("LIBPROCESS_IP", flags.ip);
  os::setenv("LIBPROCESS_PORT", flags.port);

  HttpServer* server = new HttpServer();

  process::spawn(server);
  process::wait(server->self());

  delete server;

  return EXIT_SUCCESS;
}

} // namespace tests {
} // namespace internal {
} // namespace mesos {
