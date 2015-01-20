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

#ifndef __ALLOCATION_MESOS_ALLOCATOR_HPP__
#define __ALLOCATION_MESOS_ALLOCATOR_HPP__

#include <process/dispatch.hpp>
#include <process/process.hpp>

#include "master/allocation/allocator.hpp"

namespace mesos {
namespace internal {
namespace master {
namespace allocation {

// A wrapper for Process-based allocators. It redirects all function
// invokations to the underlying AllocatorProcess and manages its
// lifetime. There is no need to check whether template parameter
// AllocatorProcess implements MesosAllocatorProcess, since it is
// implicitly ensured by invoking all interface functions.
//
// TODO(alexr): Move this class (together with the implementation)
// into a separate file.
template <typename AllocatorProcess>
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

  AllocatorProcess* process;
};


// The basic interface for all Process-based allocators.
class MesosAllocatorProcess : public process::Process<MesosAllocatorProcess>
{
public:
  MesosAllocatorProcess() {}

  virtual ~MesosAllocatorProcess() {}

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


template <typename AllocatorProcess>
MesosAllocator<AllocatorProcess>::MesosAllocator()
{
  process = new AllocatorProcess();
  process::spawn(process);
}


template <typename AllocatorProcess>
MesosAllocator<AllocatorProcess>::~MesosAllocator()
{
  process::terminate(process);
  process::wait(process);
  delete process;
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::initialize(
    const Flags& flags,
    const lambda::function<
        void(const FrameworkID&,
             const hashmap<SlaveID, Resources>&)>& offerCallback,
    const hashmap<std::string, RoleInfo>& roles)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::initialize,
      flags,
      offerCallback,
      roles);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::addFramework(
    const FrameworkID& frameworkId,
    const FrameworkInfo& frameworkInfo,
    const Resources& used)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::addFramework,
      frameworkId,
      frameworkInfo,
      used);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::removeFramework(
    const FrameworkID& frameworkId)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::removeFramework,
      frameworkId);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::activateFramework(
    const FrameworkID& frameworkId)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::activateFramework,
      frameworkId);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::deactivateFramework(
    const FrameworkID& frameworkId)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::deactivateFramework,
      frameworkId);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::addSlave(
    const SlaveID& slaveId,
    const SlaveInfo& slaveInfo,
    const Resources& total,
    const hashmap<FrameworkID, Resources>& used)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::addSlave,
      slaveId,
      slaveInfo,
      total,
      used);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::removeSlave(
    const SlaveID& slaveId)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::removeSlave,
      slaveId);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::activateSlave(
    const SlaveID& slaveId)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::activateSlave,
      slaveId);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::deactivateSlave(
    const SlaveID& slaveId)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::deactivateSlave,
      slaveId);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::updateWhitelist(
    const Option<hashset<std::string> >& whitelist)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::updateWhitelist,
      whitelist);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::requestResources(
    const FrameworkID& frameworkId,
    const std::vector<Request>& requests)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::requestResources,
      frameworkId,
      requests);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::transformAllocation(
    const FrameworkID& frameworkId,
    const SlaveID& slaveId,
    const process::Shared<Resources::Transformation>& transformation)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::transformAllocation,
      frameworkId,
      slaveId,
      transformation);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::recoverResources(
    const FrameworkID& frameworkId,
    const SlaveID& slaveId,
    const Resources& resources,
    const Option<Filters>& filters)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::recoverResources,
      frameworkId,
      slaveId,
      resources,
      filters);
}


template <typename AllocatorProcess>
inline void MesosAllocator<AllocatorProcess>::reviveOffers(
    const FrameworkID& frameworkId)
{
  process::dispatch(
      process,
      &MesosAllocatorProcess::reviveOffers,
      frameworkId);
}

} // namespace allocation {
} // namespace master {
} // namespace internal {
} // namespace mesos {

#endif // __ALLOCATION_MESOS_ALLOCATOR_HPP__
