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

#include "portal_proxy.h"

#include "spdlog/spdlog.h"

PortalProxy::PortalProxy()
    : session_bus_(plugin_common_sdbus::SessionDBus::Instance()),
      system_bus_(plugin_common_sdbus::SystemDBus::Instance()) {}

std::shared_ptr<sdbus::IProxy> PortalProxy::GetProxy(
    const PortalInterface& portal) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  // check if proxy alive, if it is, it'd not create one

  auto it = proxy_cache_.find(portal);
  if (it != proxy_cache_.end()) {
    if (auto proxy = it->second.lock()) {
      return proxy;
    }
    // clean cache if it's died
    proxy_cache_.erase(it);
  }
  try {
    sdbus::IConnection* bus = nullptr;
    if (portal.bus_type == BUS_TYPE::SYSTEM) {
      bus = &system_bus_.GetConnection();
    } else if (portal.bus_type == BUS_TYPE::SESSION) {
      bus = &session_bus_.GetConnection();
    }

    if (!bus) {
      throw std::runtime_error("Invalid bus type");
    }

    auto new_proxy =
        sdbus::createProxy(*bus, sdbus::ServiceName{portal.service_name},
                           sdbus::ObjectPath{portal.object_path});

    auto proxy = std::shared_ptr<sdbus::IProxy>(std::move(new_proxy));
    proxy_cache_[portal] = proxy;
    return proxy;
  } catch (const sdbus::Error& e) {
    spdlog::error(
        "[FlatpakPlugin] sdbus::Error Failed to create proxy for {}: {}",
        portal.service_name, e.what());
    throw;
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Failed to create proxy for {}: {}",
                  portal.service_name, e.what());
    throw;
  }
}

void PortalProxy::cleanup() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  for (auto it = proxy_cache_.begin(); it != proxy_cache_.end();) {
    if (it->second.expired()) {
      it = proxy_cache_.erase(it);
    } else {
      ++it;
    }
  }
}