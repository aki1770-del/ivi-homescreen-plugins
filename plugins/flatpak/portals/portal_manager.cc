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

#include "portal_manager.h"

#include "asio/post.hpp"
#include "spdlog/spdlog.h"

namespace flatpak_plugin {

PortalManager::PortalManager(asio::io_context& io_context)
    : io_context_(io_context), portal_proxy_(std::make_unique<PortalProxy>()) {}

PortalProxy& PortalManager::GetPortalProxy() {
  if (!portal_proxy_) {
    portal_proxy_ = std::make_unique<PortalProxy>();
  }
  return *portal_proxy_;
}

void PortalManager::register_application(
    const std::string& app_id,
    const std::vector<PortalInterface>& interfaces) {
  std::lock_guard<std::mutex> lock(app_mutex_);
  // create application context
  PortalContext Context(app_id, io_context_);
  Context.interfaces = interfaces;

  // create proxies for all interfaces
  for (auto& interface : interfaces) {
    auto proxy = GetPortalProxy().GetProxy(interface);
    Context.owned_proxies.emplace_back(proxy);
  }
  app_context_.emplace(app_id, std::move(Context));
}

void PortalManager::unregister_application(const std::string& app_id) {
  std::lock_guard<std::mutex> lock(app_mutex_);
  auto it = app_context_.find(app_id);
  if (it == app_context_.end()) {
    return;
  }
  PortalContext& Context = it->second;
  Context.owned_proxies.clear();
  app_context_.erase(it);
}

template <typename ResultCallback, typename... Args>
void PortalManager::call_method(const std::string& app_id,
                                const PortalInterface& interface,
                                const std::string& method_name,
                                ResultCallback&& callback,
                                Args&&... args) {
  auto& Context = get_app_context(app_id);

  asio::post(*Context.strand, [=]() {
    auto proxy = GetPortalProxy().GetProxy(interface);
    proxy->callMethodAsync(method_name)
        .onInterface(interface.interface_name)
        .withArguments(std::forward<Args>(args)...)
        .uponReplyInvoke([callback, strand = Context.strand.get()](
                             const sdbus::Error* error, auto... results) {
          asio::post(*strand, [=]() {
            if (error) {
              callback(error, results...);
            } else {
              callback(nullptr, results...);
            }
          });
        });
  });
}

PortalContext& PortalManager::get_app_context(const std::string& app_id) {
  std::lock_guard<std::mutex> lock(app_mutex_);
  const auto it = app_context_.find(app_id);
  if (it == app_context_.end()) {
    spdlog::error(
        "[FlatpakPlugin] PortalManager::get_app_context: app_id not found");
    throw std::runtime_error(
        "PortalManager::get_app_context: app_id not found");
  }
  return it->second;
}

}  // namespace flatpak_plugin
