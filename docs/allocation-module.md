---
layout: documentation
---

# Mesos Allocation Module

The logic that the Mesos master uses to determine which frameworks to make resource offers to is encapsulated in the Master's _allocator module_.  Allocator is a pluggable component that organizations can use to implement their own sharing policy, e.g. fair-sharing, Dominant Resource Fairness (see [the DRF paper](http://www.eecs.berkeley.edu/Pubs/TechRpts/2010/EECS-2010-55.pdf)), priority, etc.

To get a custom allocation logic in Mesos, one need to:

- [implement](#writing-a-custom-allocator) an `Allocator` interface defined in `mesos/master/allocator.hpp`,

- [wrap it](#wiring-up-a-custom-allocator) into an allocator module and load it in Mesos master.

## Writing a custom allocator

Allocator modules are implemented in C++, the language Mesos is written in. They must subclass the `Allocator` interface defined in `mesos/master/allocator.hpp`. However, your implementation can be a C++ proxy, which delegates the calls to the actual allocator written in a language of your choice.

The default allocator is `HierarchicalDRFAllocatorProcess`, which lives in `$MESOS_HOME/src/master/allocator/mesos/hierarchical.hpp`. As almost every Mesos component, it is actor-based, which means all interface methods are non-blocking and return immediately after putting the corresponding action into the actor's queue. If you would like to design your custom allocator in a similar manner, subclass `MesosAllocatorProcess` from `$MESOS_HOME/src/master/allocator/mesos/allocator.hpp` and wrap your actor-based allocator into `MesosAllocator`, which dispatches the calls to the underlying actor and controls its lifetime. You can reference `HierarchicalDRFAllocatorProcess` as a starting place if you choose to write your own actor-based allocation module.


Additionally, the built-in hierarchical allocator can be extended without the need to reimplement the entirety of the allocation logic. This is possible through the use of the `Sorter` abstraction. Sorters define the order that roles or frameworks should be offered resources in by taking "client" objects and some information about those clients and returning an ordered list of clients.

Sorters are implemented in C++ and inherit the `Sorter` class defined in `$MESOS_HOME/src/master/allocator/sorter/sorter.hpp`. The default sorter is `DRFSorter`, which implements fair sharing and can be found in `$MESOS_HOME/src/master/allocator/sorter/drf/sorter.hpp`. For `DRFSorter`, if weights are specified in `Sorter::add()`, a client's share will be divided by the weight, creating a form of priority. For example, a role that has a weight of 2 will be offered twice as many resources as a role with weight 1.

## Wiring up a custom allocator

When a custom allocator is ready, the next step is to override the built-in implementation with your own. This process consists of several steps:

- wrap your allocator into a Mesos allocator module,

- load this module in Mesos master.

An allocator module is basically a factory function and a module description, as defined in `mesos/module/allocator.hpp`. Assuming the allocation logic is implemented by the `ExternalAllocator` class declared in `external_allocator.hpp`, the following snippet describes the implementation of an allocator module named `ExternalAllocatorModule`:

```
#include <mesos/master/allocator.hpp>
#include <mesos/module/allocator.hpp>
#include <stout/try.hpp>

#include "external_allocator.hpp"

using namespace mesos;
using mesos::master::allocator::Allocator;
using mesos::internal::master::allocator::HierarchicalDRFAllocator;

static Allocator* createExternalAllocator(const Parameters& parameters)
{
  Try<Allocator*> allocator = ExternalAllocator::create();
  if (allocator.isError()) {
    return NULL;
  }

  return allocator.get();
}

// Declares an ExternalAllocator module named 'ExternalAllocatorModule'.
mesos::modules::Module<Allocator> ExternalAllocatorModule(
    MESOS_MODULE_API_VERSION,
    MESOS_VERSION,
    "Apache Mesos",
    "modules@mesos.apache.org",
    "External Allocator module.",
    NULL,
    createExternalAllocator);
```

Refer to the [Mesos Modules documentation](http://mesos.apache.org/documentation/latest/modules/) for instructions how to compile and load a module in Mesos master.
