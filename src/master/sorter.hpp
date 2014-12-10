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

#ifndef __SORTER_HPP__
#define __SORTER_HPP__

#include <list>

#include <stout/hashmap.hpp>

namespace mesos {
namespace internal {
namespace master {
namespace allocator {

// Sorters implement the logic for determining the
// order in which users or frameworks should receive
// resource allocations.
class Sorter
{
public:
  virtual ~Sorter() {}

  // Adds a client to allocate resources to. A client
  // may be a user or a framework.
  virtual void add(const std::string& client, double weight = 1) = 0;

  // Removes a client.
  virtual void remove(const std::string& client) = 0;

  // Readds a client to the sort after deactivate.
  virtual void activate(const std::string& client) = 0;

  // Removes a client from the sort, so it won't get allocated to.
  virtual void deactivate(const std::string& client) = 0;

  // Specify that resources have been allocated to the given client.
  virtual void allocated(const std::string& client,
                         const Resources& resources) = 0;

  // Specify that resources have been unallocated from the given client.
  virtual void unallocated(const std::string& client,
                           const Resources& resources) = 0;

  // Returns the resources that have been allocated to this client.
  virtual Resources allocation(const std::string& client) = 0;

  // Add resources to the total pool of resources this
  // Sorter should consider.
  virtual void add(const Resources& resources) = 0;

  // Remove resources from the total pool.
  virtual void remove(const Resources& resources) = 0;

  // Returns a list of all clients, in the order that they
  // should be allocated to, according to this Sorter's policy.
  virtual std::list<std::string> sort() = 0;

  // Returns true if this Sorter contains the specified client,
  // either active or deactivated.
  virtual bool contains(const std::string& client) = 0;

  // Returns the number of clients this Sorter contains,
  // either active or deactivated.
  virtual int count() = 0;

  virtual hashmap<std::string, std::pair<Resources, Duration>> usageHistory() = 0;

  virtual resetUsageHistory() = 0;
};

} // namespace allocator {
} // namespace master {
} // namespace internal {
} // namespace mesos {

#endif // __SORTER_HPP__
