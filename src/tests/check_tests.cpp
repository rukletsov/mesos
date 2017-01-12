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

#include <mesos/mesos.hpp>

#include "checks/checker.hpp"

#include "tests/mesos.hpp"

namespace mesos {
namespace internal {
namespace tests {

class CheckTest : public MesosTest {};


// This tests ensures `CheckInfo` protobuf is validated correctly.
TEST_F(CheckTest, CheckInfoValidation)
{
  using namespace mesos::internal::checks;

  // Check type must be set to a known value.
  {
    CheckInfo checkInfo;

    Option<Error> validate = validation::checkInfo(checkInfo);
    EXPECT_SOME(validate);

    checkInfo.set_type(CheckInfo::UNKNOWN);
    validate = validation::checkInfo(checkInfo);
    EXPECT_SOME(validate);
  }

  // The associated message for a given type must be set.
  {
    CheckInfo checkInfo;

    checkInfo.set_type(CheckInfo::COMMAND);
    Option<Error> validate = validation::checkInfo(checkInfo);
    EXPECT_SOME(validate);

    checkInfo.set_type(CheckInfo::HTTP);
    validate = validation::checkInfo(checkInfo);
    EXPECT_SOME(validate);
  }

  // Command check must specify an actual command in `command.command.value`.
  {
    CheckInfo checkInfo;

    checkInfo.set_type(CheckInfo::COMMAND);
    checkInfo.mutable_command()->CopyFrom(CheckInfo::Command());
    Option<Error> validate = validation::checkInfo(checkInfo);
    EXPECT_SOME(validate);

    checkInfo.mutable_command()->mutable_command()->CopyFrom(CommandInfo());
    validate = validation::checkInfo(checkInfo);
    EXPECT_SOME(validate);
  }

  // HTTP check may specify a path starting with '/'.
  {
    CheckInfo checkInfo;

    checkInfo.set_type(CheckInfo::HTTP);
    checkInfo.mutable_http()->set_port(8080);

    Option<Error> validate = validation::checkInfo(checkInfo);
    EXPECT_NONE(validate);

    checkInfo.mutable_http()->set_path("healthz");
    validate = validation::checkInfo(checkInfo);
    EXPECT_SOME(validate);
  }

  // Check's duration parameters must be non-negative.
  {
    CheckInfo checkInfo;

    checkInfo.set_type(CheckInfo::HTTP);
    checkInfo.mutable_http()->set_port(8080);

    checkInfo.set_delay_seconds(-1.0);
    Option<Error> validate = validation::checkInfo(checkInfo);
    EXPECT_SOME(validate);

    checkInfo.set_delay_seconds(0.0);
    checkInfo.set_interval_seconds(-1.0);
    validate = validation::checkInfo(checkInfo);
    EXPECT_SOME(validate);

    checkInfo.set_interval_seconds(0.0);
    checkInfo.set_timeout_seconds(-1.0);
    validate = validation::checkInfo(checkInfo);
    EXPECT_SOME(validate);

    checkInfo.set_timeout_seconds(0.0);
    validate = validation::checkInfo(checkInfo);
    EXPECT_NONE(validate);
  }
}


// This tests ensures `CheckStatusInfo` protobuf is validated correctly.
TEST_F(CheckTest, CheckStatusInfoValidation)
{
  using namespace mesos::internal::checks;

  // Check status type must be set to a known value.
  {
    CheckStatusInfo checkStatusInfo;

    Option<Error> validate = validation::checkStatusInfo(checkStatusInfo);
    EXPECT_SOME(validate);

    checkStatusInfo.set_type(CheckInfo::UNKNOWN);
    validate = validation::checkStatusInfo(checkStatusInfo);
    EXPECT_SOME(validate);
  }

  // The associated message for a given type must be set.
  {
    CheckStatusInfo checkStatusInfo;

    checkStatusInfo.set_type(CheckInfo::COMMAND);
    Option<Error> validate = validation::checkStatusInfo(checkStatusInfo);
    EXPECT_SOME(validate);

    checkStatusInfo.set_type(CheckInfo::HTTP);
    validate = validation::checkStatusInfo(checkStatusInfo);
    EXPECT_SOME(validate);
  }
}

} // namespace tests {
} // namespace internal {
} // namespace mesos {
