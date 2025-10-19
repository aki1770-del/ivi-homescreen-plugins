/*
 * Copyright 2023-2025 Toyota Connected North America
 * Copyright 2025 Ahmed Wafdy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PORTAL_BUS_H
#define PORTAL_BUS_H

#include <sdbus-c++/sdbus-c++.h>
#include <memory>
#include <mutex>

#include "portal_interface.h"

/**
 * \brief This class manages the D-Bus connections (system and session) and
 * provides methods to interact with them. All apps share the same connections
 * to session/system bus. D-Bus event signals processing is handled in async
 * mode in (non-blocking) background thread.
 */
class PortalBus {
 public:
  void init();

  sdbus::IConnection& getConnection(BUS_TYPE type);

  ~PortalBus() = default;

 private:
  std::unique_ptr<sdbus::IConnection> session_connection_;
  std::unique_ptr<sdbus::IConnection> system_connection_;
  std::mutex init_mutex_;
  bool initialized_ = false;
};

#endif  // PORTAL_BUS_H
