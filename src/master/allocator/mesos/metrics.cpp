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

#include "master/allocator/mesos/metrics.hpp"

#include <process/metrics/metrics.hpp>

#include <stout/strings.hpp>

#include "master/allocator/mesos/hierarchical.hpp"

using std::string;

namespace mesos {
namespace internal {
namespace master {
namespace allocator {
namespace internal {

Metrics::Metrics(const HierarchicalAllocatorProcess& allocator)
  : event_queue_dispatches(
        "allocator/event_queue_dispatches",
        process::defer(
            allocator.self(),
            &HierarchicalAllocatorProcess::_event_queue_dispatches)),
    allocation_runs("allocator/allocation_runs")
{
  process::metrics::add(event_queue_dispatches);
  process::metrics::add(allocation_runs);
}


Metrics::~Metrics()
{
  process::metrics::remove(event_queue_dispatches);
  process::metrics::remove(allocation_runs);

  foreachvalue (const process::metrics::Gauge& gauge, total) {
    process::metrics::remove(gauge);
  }

  foreachvalue (const process::metrics::Gauge& gauge, allocated) {
    process::metrics::remove(gauge);
  }
}


void Metrics::createGaugesForResource(
    const HierarchicalAllocatorProcess& allocator, const string& resourceName)
{
  // We check only `total` since `total` and `allocated` are kept in sync.
  if (total.contains(resourceName)) {
    return;
  }

  // Install a gauge for the total amount of resource kind `resourceName`.
  total.put(
      resourceName,
      process::metrics::Gauge(
          strings::join("/", "allocator/total", resourceName),
          process::defer(allocator.self(), [&allocator, resourceName]() {
            return allocator._total(resourceName);
          })));
  process::metrics::add(total.get(resourceName).get());

  // Install a gauge for the allocated amount of resource kind `resourceName`.
  allocated.put(
      resourceName,
      process::metrics::Gauge(
          strings::join("/", "allocator/allocated", resourceName),
          process::defer(allocator.self(), [&allocator, resourceName]() {
            return allocator._allocated(resourceName);
          })));
  process::metrics::add(allocated.get(resourceName).get());
}

} // namespace internal {
} // namespace allocator {
} // namespace master {
} // namespace internal {
} // namespace mesos {
