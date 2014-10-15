#ifndef __PROCESS_REAP_HPP__
#define __PROCESS_REAP_HPP__

#include <sys/types.h>

#include <process/future.hpp>

#include <stout/option.hpp>

namespace process {

// Lower and upper bounds for the poll interval in the reaper.
static inline Duration lowReaperPollInterval() { return Milliseconds(100); }
static inline Duration highReaperPollInterval() { return Seconds(1); }

// Returns the exit status of the specified process if and only if
// the process is a direct child and it has not already been reaped.
// Otherwise, returns None once the process has been reaped elsewhere
// (or does not exist, which is indistinguishable from being reaped
// elsewhere). This will never discard the returned future.
Future<Option<int> > reap(pid_t pid);

} // namespace process {

#endif // __PROCESS_REAP_HPP__
