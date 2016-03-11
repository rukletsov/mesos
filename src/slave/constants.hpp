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

#ifndef __SLAVE_CONSTANTS_HPP__
#define __SLAVE_CONSTANTS_HPP__

#include <stdint.h>

#include <stout/bytes.hpp>
#include <stout/duration.hpp>

namespace mesos {
namespace internal {
namespace slave {

// TODO(bmahler): It appears there may be a bug with gcc-4.1.2 in which these
// duration constants were not being initialized when having static linkage.
// This issue did not manifest in newer gcc's. Specifically, 4.2.1 was ok.
// So we've moved these to have external linkage but perhaps in the future
// we can revert this.

// TODO(jieyu): Use static functions for all the constants. See more
// details in MESOS-1023.

extern const Duration EXECUTOR_REGISTRATION_TIMEOUT;
extern const Duration EXECUTOR_REREGISTER_TIMEOUT;

// The default amount of time to wait for the executor to
// shut down before destroying the container.
extern const Duration DEFAULT_EXECUTOR_SHUTDOWN_GRACE_PERIOD;

extern const Duration RECOVERY_TIMEOUT;

extern const Duration STATUS_UPDATE_RETRY_INTERVAL_MIN;
extern const Duration STATUS_UPDATE_RETRY_INTERVAL_MAX;

// Default backoff interval used by the slave to wait before registration.
extern const Duration DEFAULT_REGISTRATION_BACKOFF_FACTOR;

// The maximum interval the slave waits before retrying registration.
// Note that this value has to be << 'MIN_SLAVE_REREGISTER_TIMEOUT'
// declared in 'master/constants.hpp'. This helps the slave to retry
// (re-)registration multiple times between when the master finishes
// recovery and when it times out slave re-registration.
extern const Duration REGISTER_RETRY_INTERVAL_MAX;

extern const Duration GC_DELAY;
extern const Duration DISK_WATCH_INTERVAL;

// Minimum free disk capacity enforced by the garbage collector.
extern const double GC_DISK_HEADROOM;

// Maximum number of completed frameworks to store in memory.
extern const uint32_t MAX_COMPLETED_FRAMEWORKS;

// Maximum number of completed executors per framework to store in memory.
extern const uint32_t MAX_COMPLETED_EXECUTORS_PER_FRAMEWORK;

// Maximum number of completed tasks per executor to store in memory.
extern const uint32_t MAX_COMPLETED_TASKS_PER_EXECUTOR;

// Default cpus offered by the slave.
extern const double DEFAULT_CPUS;

// Default memory offered by the slave.
extern const Bytes DEFAULT_MEM;

// Default disk space offered by the slave.
extern const Bytes DEFAULT_DISK;

// Default ports range offered by the slave.
extern const std::string DEFAULT_PORTS;

// Default cpu resource given to a command executor.
extern const double DEFAULT_EXECUTOR_CPUS;

// Default memory resource given to a command executor.
extern const Bytes DEFAULT_EXECUTOR_MEM;

#ifdef WITH_NETWORK_ISOLATOR
// Default number of ephemeral ports allocated to a container by the
// network isolator.
extern const uint16_t DEFAULT_EPHEMERAL_PORTS_PER_CONTAINER;
#endif

// Default duration that docker containers will be removed after exit.
extern const Duration DOCKER_REMOVE_DELAY;

// Default duration to wait before retry inspecting a docker
// container.
extern const Duration DOCKER_INSPECT_DELAY;

// Default maximum number of docker inspect calls docker ps will invoke
// in parallel to prevent hitting system's open file descriptor limit.
extern const uint32_t DOCKER_PS_MAX_INSPECT_CALLS;

// Default duration that docker containerizer will wait to check
// docker version.
extern const Duration DOCKER_VERSION_WAIT_TIMEOUT;

// Name of the default, CRAM-MD5 authenticatee.
extern const std::string DEFAULT_AUTHENTICATEE;

// Default maximum storage space to be used by the fetcher cache.
extern const Bytes DEFAULT_FETCHER_CACHE_SIZE;

// If no pings received within this timeout, then the slave will
// trigger a re-detection of the master to cause a re-registration.
Duration DEFAULT_MASTER_PING_TIMEOUT();

// Container path that the slave sets to mount the command executor rootfs to.
extern const std::string COMMAND_EXECUTOR_ROOTFS_CONTAINER_PATH;

} // namespace slave {
} // namespace internal {
} // namespace mesos {

#endif // __SLAVE_CONSTANTS_HPP__
