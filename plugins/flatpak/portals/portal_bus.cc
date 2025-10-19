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

#include "portal_bus.h"

void PortalBus::init() {
  std::lock_guard<std::mutex> lock(init_mutex_);
  if (initialized_) {
    return;
  }
  // create session bus and enter async loop
  session_connection_ = sdbus::createSessionBusConnection();
  session_connection_->enterEventLoopAsync();
  // create system bus and enter async loop
  system_connection_ = sdbus::createSystemBusConnection();
  system_connection_->enterEventLoopAsync();
  initialized_ = true;
}

sdbus::IConnection& PortalBus::getConnection(BUS_TYPE type) {
  if (!initialized_) {
    init();
  }
  return (type == BUS_TYPE::SESSION) ? *session_connection_
                                     : *system_connection_;
}
