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

#include <string>
#include <vector>

#include <glog/logging.h>

#include <process/delay.hpp>
#include <process/id.hpp>

#include <stout/foreach.hpp>
#include <stout/os.hpp>
#include <stout/strings.hpp>
#include <stout/try.hpp>

#include "watcher/whitelist_watcher.hpp"

namespace mesos {
namespace internal {

using std::string;
using std::vector;

using process::Process;

using lambda::function;

Duration WHITELIST_WATCH_INTERVAL() { return Seconds(5); }


WhitelistWatcher::WhitelistWatcher(
    const string& path,
    const function<
      void(const Option<hashset<string>>& whitelist)>& subscriber)
  : ProcessBase(process::ID::generate("whitelist")),
    path(path),
    subscriber(subscriber) {}


void WhitelistWatcher::initialize()
{
  watch();
}


void WhitelistWatcher::watch()
{
  // Get the list of white listed nodes.
  Option<hashset<string>> whitelist;
  if (path == "*") { // Accept all nodes.
    VLOG(1) << "No whitelist given";
  } else {
    // Read from local file.
    // TODO(vinod): Add support for reading from ZooKeeper.
    // TODO(vinod): Ensure this read is atomic w.r.t external
    // writes/updates to this file.
    Try<string> read = os::read(
        strings::remove(path, "file://", strings::PREFIX));
    if (read.isError()) {
      LOG(ERROR) << "Error reading whitelist file: " << read.error() << ". "
                 << "Retrying";
      whitelist = lastWhitelist;
    } else if (read.get().empty()) {
      VLOG(1) << "Empty whitelist file " << path;
      whitelist = hashset<string>();
    } else {
      hashset<string> hostnames;
      vector<string> lines = strings::tokenize(read.get(), "\n");
      foreach (const string& hostname, lines) {
        hostnames.insert(hostname);
      }
      whitelist = hostnames;
    }
  }

  // Send the whitelist to subscriber, if necessary.
  if (whitelist != lastWhitelist) {
    subscriber(whitelist);
  }

  // Schedule the next check.
  lastWhitelist = whitelist;
  delay(WHITELIST_WATCH_INTERVAL(), self(), &WhitelistWatcher::watch);
}

} // namespace internal {
} // namespace mesos {
