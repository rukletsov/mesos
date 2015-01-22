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

#ifndef __ALLOCATION_ALLOCATOR_HPP__
#define __ALLOCATION_ALLOCATOR_HPP__

#include <string>

#include <mesos/resources.hpp>

#include <process/future.hpp>
#include <process/dispatch.hpp>
#include <process/pid.hpp>
#include <process/process.hpp>
#include <process/shared.hpp>

#include <stout/hashmap.hpp>
#include <stout/hashset.hpp>
#include <stout/lambda.hpp>
#include <stout/option.hpp>

#include "master/flags.hpp"

#include "messages/messages.hpp"

namespace mesos {
namespace internal {
namespace master {

class Master; // Forward declaration.

namespace allocator {

// Basic model of an allocator: resources are allocated to a framework
// in the form of offers. A framework can refuse some resources in
// offers and run tasks in others. Allocated resources can have
// transformations applied to them in order for frameworks to alter
// the resource metadata (e.g. persistent disk). Resources can be
// recovered from a framework when tasks finish/fail (or are lost
// due to a slave failure) or when an offer is rescinded.
//
// This is the public API for resource allocators.
// TODO(alexr): Document API calls.
class Allocator
{
public:
  Allocator() {}

  virtual ~Allocator() {}

  virtual void initialize(
      const Flags& flags,
      const lambda::function<
          void(const FrameworkID&,
               const hashmap<SlaveID, Resources>&)>& offerCallback,
      const hashmap<std::string, RoleInfo>& roles) = 0;

  virtual void addFramework(
      const FrameworkID& frameworkId,
      const FrameworkInfo& frameworkInfo,
      const Resources& used) = 0;

  virtual void removeFramework(
      const FrameworkID& frameworkId) = 0;

  // Offers are sent only to activated frameworks.
  virtual void activateFramework(
      const FrameworkID& frameworkId) = 0;

  virtual void deactivateFramework(
      const FrameworkID& frameworkId) = 0;

  // Note that the 'total' resources are passed explicitly because it
  // includes resources that are dynamically "persisted" on the slave
  // (e.g. persistent volumes, dynamic reservations, etc).
  // The slaveInfo resources, on the other hand, correspond directly
  // to the static --resources flag value on the slave.
  virtual void addSlave(
      const SlaveID& slaveId,
      const SlaveInfo& slaveInfo,
      const Resources& total,
      const hashmap<FrameworkID, Resources>& used) = 0;

  virtual void removeSlave(
      const SlaveID& slaveId) = 0;

  // Offers are sent only for activated slaves.
  virtual void activateSlave(
      const SlaveID& slaveId) = 0;

  virtual void deactivateSlave(
      const SlaveID& slaveId) = 0;

  virtual void updateWhitelist(
      const Option<hashset<std::string> >& whitelist) = 0;

  virtual void requestResources(
      const FrameworkID& frameworkId,
      const std::vector<Request>& requests) = 0;

  virtual void transformAllocation(
      const FrameworkID& frameworkId,
      const SlaveID& slaveId,
      const process::Shared<Resources::Transformation>& transformation) = 0;

  // Informs the Allocator to recover resources that are considered
  // used by the framework.
  virtual void recoverResources(
      const FrameworkID& frameworkId,
      const SlaveID& slaveId,
      const Resources& resources,
      const Option<Filters>& filters) = 0;

  // Whenever a framework that has filtered resources wants to revive
  // offers for those resources the master invokes this callback.
  virtual void reviveOffers(
      const FrameworkID& frameworkId) = 0;
};


// A wrapper for AllocatorProcess-based allocators. It redirects all
// function invokations to the underlying  AllocatorProcess and
// manages its lifetime. There is no need to check whether template
// parameter MesosAllocatorProcess implements AllocatorProcess, since
// it is implicitly ensured by invoking all interface functions.
//
// TODO(alexr): Move this class (together with the implementation)
// into a separate file.
template <typename MesosAllocatorProcess>
class MesosAllocator : public Allocator
{
public:
  MesosAllocator();

  ~MesosAllocator();

  void initialize(
      const Flags& flags,
      const lambda::function<
          void(const FrameworkID&,
               const hashmap<SlaveID, Resources>&)>& offerCallback,
      const hashmap<std::string, RoleInfo>& roles);

  void addFramework(
      const FrameworkID& frameworkId,
      const FrameworkInfo& frameworkInfo,
      const Resources& used);

  void removeFramework(
      const FrameworkID& frameworkId);

  // Offers are sent only to activated frameworks.
  void activateFramework(
      const FrameworkID& frameworkId);

  void deactivateFramework(
      const FrameworkID& frameworkId);

  // Note that the 'total' resources are passed explicitly because it
  // includes resources that are dynamically "persisted" on the slave
  // (e.g. persistent volumes, dynamic reservations, etc).
  // The slaveInfo resources, on the other hand, correspond directly
  // to the static --resources flag value on the slave.
  void addSlave(
      const SlaveID& slaveId,
      const SlaveInfo& slaveInfo,
      const Resources& total,
      const hashmap<FrameworkID, Resources>& used);

  void removeSlave(
      const SlaveID& slaveId);

  // Offers are sent only for activated slaves.
  void activateSlave(
      const SlaveID& slaveId);

  void deactivateSlave(
      const SlaveID& slaveId);

  void updateWhitelist(
      const Option<hashset<std::string> >& whitelist);

  void requestResources(
      const FrameworkID& frameworkId,
      const std::vector<Request>& requests);

  void transformAllocation(
      const FrameworkID& frameworkId,
      const SlaveID& slaveId,
      const process::Shared<Resources::Transformation>& transformation);

  // Informs the allocator to recover resources that are considered
  // used by the framework.
  void recoverResources(
      const FrameworkID& frameworkId,
      const SlaveID& slaveId,
      const Resources& resources,
      const Option<Filters>& filters);

  // Whenever a framework that has filtered resources wants to revive
  // offers for those resources the master invokes this callback.
  void reviveOffers(
      const FrameworkID& frameworkId);

private:
  MesosAllocator(const MesosAllocator&); // Not copyable.
  MesosAllocator& operator=(const MesosAllocator&); // Not assignable.

  MesosAllocatorProcess* process;
};


// The basic interface for all Process-based allocators.
class AllocatorProcess : public process::Process<AllocatorProcess>
{
public:
  AllocatorProcess() {}

  virtual ~AllocatorProcess() {}

  // Explicitly unhide 'initialize' to silence a compiler warning
  // from clang, since we overload below.
  using process::ProcessBase::initialize;

  virtual void initialize(
      const Flags& flags,
      const lambda::function<
          void(const FrameworkID&,
               const hashmap<SlaveID, Resources>&)>& offerCallback,
      const hashmap<std::string, RoleInfo>& roles) = 0;

  virtual void addFramework(
      const FrameworkID& frameworkId,
      const FrameworkInfo& frameworkInfo,
      const Resources& used) = 0;

  virtual void removeFramework(
      const FrameworkID& frameworkId) = 0;

  virtual void activateFramework(
      const FrameworkID& frameworkId) = 0;

  virtual void deactivateFramework(
      const FrameworkID& frameworkId) = 0;

  virtual void addSlave(
      const SlaveID& slaveId,
      const SlaveInfo& slaveInfo,
      const Resources& total,
      const hashmap<FrameworkID, Resources>& used) = 0;

  virtual void removeSlave(
      const SlaveID& slaveId) = 0;

  virtual void activateSlave(
      const SlaveID& slaveId) = 0;

  virtual void deactivateSlave(
      const SlaveID& slaveId) = 0;

  virtual void updateWhitelist(
      const Option<hashset<std::string> >& whitelist) = 0;

  virtual void requestResources(
      const FrameworkID& frameworkId,
      const std::vector<Request>& requests) = 0;

  virtual void transformAllocation(
      const FrameworkID& frameworkId,
      const SlaveID& slaveId,
      const process::Shared<Resources::Transformation>& transformation) = 0;

  virtual void recoverResources(
      const FrameworkID& frameworkId,
      const SlaveID& slaveId,
      const Resources& resources,
      const Option<Filters>& filters) = 0;

  virtual void reviveOffers(
      const FrameworkID& frameworkId) = 0;
};


template <typename MesosAllocatorProcess>
MesosAllocator<MesosAllocatorProcess>::MesosAllocator()
{
  process = new MesosAllocatorProcess();
  process::spawn(process);
}


template <typename MesosAllocatorProcess>
MesosAllocator<MesosAllocatorProcess>::~MesosAllocator()
{
  process::terminate(process);
  process::wait(process);
  delete process;
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::initialize(
    const Flags& flags,
    const lambda::function<
        void(const FrameworkID&,
             const hashmap<SlaveID, Resources>&)>& offerCallback,
    const hashmap<std::string, RoleInfo>& roles)
{
  process::dispatch(
      process,
      &AllocatorProcess::initialize,
      flags,
      offerCallback,
      roles);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::addFramework(
    const FrameworkID& frameworkId,
    const FrameworkInfo& frameworkInfo,
    const Resources& used)
{
  process::dispatch(
      process,
      &AllocatorProcess::addFramework,
      frameworkId,
      frameworkInfo,
      used);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::removeFramework(
    const FrameworkID& frameworkId)
{
  process::dispatch(
      process,
      &AllocatorProcess::removeFramework,
      frameworkId);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::activateFramework(
    const FrameworkID& frameworkId)
{
  process::dispatch(
      process,
      &AllocatorProcess::activateFramework,
      frameworkId);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::deactivateFramework(
    const FrameworkID& frameworkId)
{
  process::dispatch(
      process,
      &AllocatorProcess::deactivateFramework,
      frameworkId);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::addSlave(
    const SlaveID& slaveId,
    const SlaveInfo& slaveInfo,
    const Resources& total,
    const hashmap<FrameworkID, Resources>& used)
{
  process::dispatch(
      process,
      &AllocatorProcess::addSlave,
      slaveId,
      slaveInfo,
      total,
      used);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::removeSlave(
    const SlaveID& slaveId)
{
  process::dispatch(
      process,
      &AllocatorProcess::removeSlave,
      slaveId);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::activateSlave(
    const SlaveID& slaveId)
{
  process::dispatch(
      process,
      &AllocatorProcess::activateSlave,
      slaveId);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::deactivateSlave(
    const SlaveID& slaveId)
{
  process::dispatch(
      process,
      &AllocatorProcess::deactivateSlave,
      slaveId);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::updateWhitelist(
    const Option<hashset<std::string> >& whitelist)
{
  process::dispatch(
      process,
      &AllocatorProcess::updateWhitelist,
      whitelist);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::requestResources(
    const FrameworkID& frameworkId,
    const std::vector<Request>& requests)
{
  process::dispatch(
      process,
      &AllocatorProcess::requestResources,
      frameworkId,
      requests);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::transformAllocation(
    const FrameworkID& frameworkId,
    const SlaveID& slaveId,
    const process::Shared<Resources::Transformation>& transformation)
{
  process::dispatch(
      process,
      &AllocatorProcess::transformAllocation,
      frameworkId,
      slaveId,
      transformation);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::recoverResources(
    const FrameworkID& frameworkId,
    const SlaveID& slaveId,
    const Resources& resources,
    const Option<Filters>& filters)
{
  process::dispatch(
      process,
      &AllocatorProcess::recoverResources,
      frameworkId,
      slaveId,
      resources,
      filters);
}


template <typename MesosAllocatorProcess>
inline void MesosAllocator<MesosAllocatorProcess>::reviveOffers(
    const FrameworkID& frameworkId)
{
  process::dispatch(
      process,
      &AllocatorProcess::reviveOffers,
      frameworkId);
}

} // namespace allocator {
} // namespace master {
} // namespace internal {
} // namespace mesos {

#endif // __ALLOCATION_ALLOCATOR_HPP__
