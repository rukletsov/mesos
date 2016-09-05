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

#ifndef __HEALTH_CHECKER_HPP__
#define __HEALTH_CHECKER_HPP__

#include <string>
#include <tuple>
#include <vector>

#include <mesos/mesos.hpp>

#include <process/future.hpp>
#include <process/owned.hpp>
#include <process/pid.hpp>
#include <process/process.hpp>
#include <process/protobuf.hpp>
#include <process/time.hpp>

#include <stout/duration.hpp>
#include <stout/lambda.hpp>
#include <stout/nothing.hpp>

#include "messages/messages.hpp"

namespace mesos {
namespace internal {
namespace health {

// Forward declarations.
class HealthCheckerProcess;

class HealthChecker
{
public:
  /**
   * Attempts to create a `HealthChecker` object.
   *
   * @param check The protobuf message definition of health check.
   * @param callback A callback HealthChecker uses to send health status
   *     updates to its owner (usually an executor).
   * @param taskID The TaskID of the target task.
   * @param taskPid The target task's pid used to enter the specified
   *     namespaces.
   * @param namespaces The namespaces to enter prior performing a single health
   *     check.
   * @return A `HealthChecker` object or an error if `create` fails.
   */
  static Try<process::Owned<HealthChecker>> create(
      const HealthCheck& check,
      const lambda::function<void(const TaskHealthStatus&)>& callback,
      const std::string& launcherDir,
      const TaskID& taskID,
      Option<pid_t> taskPid,
      const std::vector<std::string>& namespaces);

  ~HealthChecker();

  void healthCheck();

  // Idempotent helpers for pausing and resuming health checking.
  void pause();
  void resume();

private:
  explicit HealthChecker(process::Owned<HealthCheckerProcess> process);

  process::Owned<HealthCheckerProcess> process;
};


class HealthCheckerProcess : public ProtobufProcess<HealthCheckerProcess>
{
public:
  HealthCheckerProcess(
      const HealthCheck& _check,
      const lambda::function<void(const TaskHealthStatus&)>& _callback,
      const std::string& _launcherDir,
      const TaskID& _taskID,
      Option<pid_t> _taskPid,
      const std::vector<std::string>& _namespaces);

  virtual ~HealthCheckerProcess() {}

  void healthCheck();

  void pause();
  void resume();

private:
  void failure(const std::string& message);
  void success();

  void _healthCheck();

  void __healthCheck(const process::Future<Nothing>& future);

  process::Future<Nothing> _commandHealthCheck();

  process::Future<Nothing> _httpHealthCheck();

  process::Future<Nothing> __httpHealthCheck(
      const std::tuple<
          process::Future<Option<int>>,
          process::Future<std::string>,
          process::Future<std::string>>& t);

  process::Future<Nothing> _tcpHealthCheck();

  process::Future<Nothing> __tcpHealthCheck(
      const std::tuple<
          process::Future<Option<int>>,
          process::Future<std::string>,
          process::Future<std::string>>& t);

  void reschedule(const Duration& duration);

  HealthCheck check;
  lambda::function<void(const TaskHealthStatus&)> healthUpdateCallback;
  std::string launcherDir;
  bool initializing;
  TaskID taskID;
  Option<pid_t> taskPid;
  std::vector<std::string> namespaces;
  Option<lambda::function<pid_t(const lambda::function<int()>&)>> clone;
  uint32_t consecutiveFailures;
  process::Time startTime;
  bool paused;
};


namespace validation {

Option<Error> healthCheck(const HealthCheck& check);

} // namespace validation {

} // namespace health {
} // namespace internal {
} // namespace mesos {

#endif // __HEALTH_CHECKER_HPP__
