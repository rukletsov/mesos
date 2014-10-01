#ifndef __PROCESS_REAP_HPP__
#define __PROCESS_REAP_HPP__

#include <sys/types.h>

#include <process/future.hpp>

#include <stout/option.hpp>

namespace process {

// TODO(jieyu): Use static functions for all the constants. See more
// details in MESOS-1023.

// Lower and upper bounds for the poll interval in the reaper.
extern const Duration LOW_POLL_INTERVAL;
extern const Duration HIGH_POLL_INTERVAL;

// Returns the exit status of the specified process if and only if
// the process is a direct child and it has not already been reaped.
// Otherwise, returns None once the process has been reaped elsewhere
// (or does not exist, which is indistinguishable from being reaped
// elsewhere). This will never discard the returned future.
Future<Option<int> > reap(pid_t pid);

} // namespace process {

#endif // __PROCESS_REAP_HPP__
