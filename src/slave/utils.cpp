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

#include "stout/duration.hpp"

#include "logging/logging.hpp"

#include "slave/constants.hpp"
#include "slave/utils.hpp"

namespace mesos {
namespace internal {
namespace slave {

Duration calculateShutdownTimeout(
    Duration shutdownTimeout,
    int callerLevel)
{
  if (shutdownTimeout < Duration::zero()) {
    LOG(WARNING) << "Shutdown grace period should be nonnegative (got "
                 << shutdownTimeout << "), using default value: "
                 << mesos::internal::slave::EXECUTOR_SHUTDOWN_GRACE_PERIOD;
    shutdownTimeout = mesos::internal::slave::EXECUTOR_SHUTDOWN_GRACE_PERIOD;
  }

  // The number of graceful shutdown levels including the current one.
  int numLevels = (callerLevel + 1);

  // The minimal base timeout required for graceful shutdown to be
  // functional on the number of levels we currently observe.
  Duration minReasonableTimeout =
    mesos::internal::slave::SHUTDOWN_TIMEOUT_DELTA * numLevels;

  if (shutdownTimeout >= minReasonableTimeout) {
    shutdownTimeout -=
      mesos::internal::slave::SHUTDOWN_TIMEOUT_DELTA * callerLevel;
  } else {
    LOG(WARNING) << "Shutdown grace period " << shutdownTimeout
                 << " is too small; expect at least " << minReasonableTimeout
                 << " for " << numLevels << " levels";
    shutdownTimeout /= numLevels;
  }

  return shutdownTimeout;
}

} // namespace slave {
} // namespace internal {
} // namespace mesos {
