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

#include "access_portal.h"

#include <ctime>
#include <iomanip>
#include <numbers>
#include "asio/post.hpp"
#include "flatpak/flatpak_shim.h"
#include "spdlog/spdlog.h"

namespace flatpak_plugin {

 AccessPortal::AccessPortal(
    PortalManager* portal_manager,
    asio::io_context& io_context,
    flutter::EventChannel<flutter::EncodableValue>& event_channel)
    : portal_manager_(*portal_manager),
      io_context_(io_context),
      event_channel_(&event_channel),
      session_bus_(plugin_common_sdbus::SessionDBus::Instance()) {}

AccessPortal::~AccessPortal() {
   try {
     // Clean up any remaining requests
     for (auto& [handle, request] : requests_) {
       request->request_proxy->callMethod("Close")
       .onInterface("org.freedesktop.portal.Request");
     }
     requests_.clear();
   } catch (const std::exception& e) {
     spdlog::error("Failed to clean up portal requests: {}", e.what());
   }
 }

std::string AccessPortal::GenerateHandle(const std::string& app_id) {
   const std::string sender = session_bus_.GetConnection().getUniqueName();

   // Format is like ":1.234", Should be like "1_234"
   std::string token_sender = sender.substr(1);
   std::replace(token_sender.begin(), token_sender.end(), '.', '_');

   const uint32_t token_id = request_counter_.fetch_add(1);
   std::string app_id_interface = app_id;
   std::replace(app_id_interface.begin(), app_id_interface.end(), '.', '_');

   const std::string token = app_id_interface + "_" + std::to_string(token_id);

   // Generate handle
   std::string handle = "/org/freedesktop/portal/desktop/request/" + token_sender + "/" + token;

   return handle;
 }

std::string AccessPortal::ShowAccessDialog(
     const std::string& app_id,
     const std::string& parent_window,
     const AccessDialogOptions& options) {
   std::string handle = GenerateHandle(app_id);

   // create a context for the request
   auto context = std::make_unique<AccessRequest>();
   context->app_id = app_id;
   context->options = options;
   context->handle = handle;

   {
     std::lock_guard<std::mutex> lock(requests_mutex_);
     requests_[handle] = std::move(context);
   }
   // SetupRequestSignalHandler(handle);
   // std::map<std::string,sdbus::Variant> portal_options;
   //
   // portal_options["modal"] = sdbus::Variant(options.modal);
   // if (!options.deny_label.empty()) {
   //   portal_options["deny_label"] = sdbus::Variant(options.deny_label);
   // }
   // if (!options.allow_label.empty()) {
   //   portal_options["grant_label"] = sdbus::Variant(options.allow_label);
   // }
   // if (!options.icon.empty()) {
   //   portal_options["icon"] = sdbus::Variant(options.icon);
   // }

   PortalInterface access_interface;
   access_interface.service_name = "org.freedesktop.impl.portal.desktop.gtk";
   access_interface.object_path = "/org/freedesktop/portal/desktop";
   access_interface.interface_name = "org.freedesktop.impl.portal.Access";
   access_interface.bus_type = BUS_TYPE::SESSION;
   auto proxy = portal_manager_.GetPortalProxy().GetProxy(access_interface);

   try {
     // call access dialog
     // proxy->callMethodAsync("AccessDialog")
     // .onInterface("org.freedesktop.impl.portal.Access")
     // .withArguments(
     //  sdbus::ObjectPath(handle),
     //  app_id,
     //  parent_window,
     //  options.title,
     //  options.subtitle,
     //  options.body,
     //  portal_options
     // )
     // .uponReplyInvoke([this,handle](std::optional<sdbus::Error> error) {
     //   if (error) {
     //     spdlog::error("Failed to show access dialog for {}: {}", handle, error->getMessage());
     //
     //
     //   flutter::EncodableMap event;
     //                event[flutter::EncodableValue("type")] =
     //                    flutter::EncodableValue("error");
     //                event[flutter::EncodableValue("handle")] =
     //                    flutter::CustomEncodableValue(handle);
     //                event[flutter::EncodableValue("message")] =
     //                    flutter::CustomEncodableValue(error->getMessage());
     //                SendEvent(event);
     //     std::lock_guard<std::mutex> lock(requests_mutex_);
     //     requests_.erase(handle);
     //  }
     // });
     spdlog::debug("[AccessPortal] ShowAccessDialog called for {}",handle);
   } catch (const sdbus::Error& e) {
     spdlog::error("[AccessPortal] Failed to show access dialog for {}: {}", handle, e.what());
      requests_.erase(handle);
     throw;
   }
   return handle;
 }

void AccessPortal::SetupRequestSignalHandler(const std::string& handler) {
std::lock_guard<std::mutex> lock(requests_mutex_);

  const auto it = requests_.find(handler);
   if (it == requests_.end()) {
     spdlog::error("[AccessPortal] Request {} not found", handler);
     return;
   }

   AccessRequest* request = it->second.get();
   request->request_proxy = sdbus::createProxy(
       session_bus_.GetConnection(),
       sdbus::ServiceName{"org.freedesktop.portal.Desktop"},
       sdbus::ObjectPath{handler});

   // subscribe to response signal
   request->request_proxy->uponSignal("Response")
   .onInterface("org.freedesktop.portal.Request")
   .call([this,handler](uint32_t response_code, const std::map<std::string, sdbus::Variant>& results) {
     spdlog::debug("[AccessPortal] Response received for {}", handler);
     asio::post(io_context_, [this,handler,response_code,results]() {
       OnPortalResponse(handler, response_code, results);
     });
   });
   //request->request_proxy->finishRegistration();
 }

void AccessPortal::OnPortalResponse(
    const std::string& handle,
    uint32_t response_code,
    const std::map<std::string, sdbus::Variant>& results) {
spdlog::info("[AccessPortal] Response received for {}: {}", handle, response_code);
  std::unique_ptr<AccessRequest> request;

   {
  std::lock_guard<std::mutex> lock(requests_mutex_);
  auto it = requests_.find(handle);
  if (it == requests_.end()) {
    spdlog::error("[AccessPortal] Request {} not found", handle);
    return;
  }
  request = std::move(it->second);
  requests_.erase(it);
  }

   flutter::EncodableMap event;
   event[flutter::EncodableValue("type")] = flutter::EncodableValue("access_response");
   event[flutter::EncodableValue("handle")] = flutter::EncodableValue(handle);
   event[flutter::EncodableValue("response_code")] = flutter::EncodableValue(response_code);

  std::string response_status;
   switch (response_code) {
    case 0:
      response_status = "granted";
      break;
    case 1:
      response_status = "denied";
      break;
    default:
      response_status = "error";
       break;
   }
  event[flutter::EncodableValue("response_status")] = flutter::CustomEncodableValue(response_status);
   flutter::EncodableMap result_map;

for (const auto& [key, variant] : results) {
  try {
    if (variant.containsValueOfType<std::string>()) {
      result_map[flutter::CustomEncodableValue(key)] = flutter::CustomEncodableValue(variant.get<std::string>());
    }else if (variant.containsValueOfType<bool>()) {
      result_map[flutter::CustomEncodableValue(key)] = flutter::CustomEncodableValue(variant.get<bool>());
    }else if (variant.containsValueOfType<int32_t>()) {
      result_map[flutter::CustomEncodableValue(key)] = flutter::CustomEncodableValue(variant.get<int32_t>());
    }else if (variant.containsValueOfType<uint32_t>()) {
      result_map[flutter::CustomEncodableValue(key)] = flutter::CustomEncodableValue(variant.get<uint32_t>());
    }
  } catch (const std::exception& e) {
    spdlog::error("[AccessPortal] Failed to parse response {} : {}", handle ,e.what());
  }
}

event[flutter::EncodableValue("results")] = flutter::CustomEncodableValue(result_map);
   if (request->callback) {
     request->callback(response_code, results);
   }
   SendEvent(event);
}


void AccessPortal::SendEvent(const flutter::EncodableMap& event) {
if (!event_channel_) {
  spdlog::error("[AccessPortal] Event channel not set");
  return;
}
   asio::post(io_context_, [this,event]() {
    //  this->shim_->SendAccessEvent(event);
   });
}

void AccessPortal::CancelAccessRequest(const std::string& handle) {
std::lock_guard<std::mutex> lock(requests_mutex_);
   auto it = requests_.find(handle);
   if (it == requests_.end()) {
     spdlog::error("[AccessPortal] Request {} not found", handle);
     return;
   }
   it->second->request_proxy->callMethod("Close")
   .onInterface("org.freedesktop.portal.Request");
   requests_.erase(it);
   spdlog::debug("[AccessPortal] Request {} canceled", handle);
}

void AccessPortal::CheckAllPermissions(
    const std::string& app_id,
    const std::vector<std::string>& permissions,
    std::function<void(std::map<std::string, PermissionStatus>)> callback) {
   auto results = std::make_shared<std::map<std::string, PermissionStatus>>();
   auto counter = std::make_shared<std::atomic<int>>(0);
   int total = permissions.size();

   for (const auto& permission : permissions) {
     std::string table = "devices";
     std::string resource_id = permission;
     if (permission == "location") {
        table = "location";
       resource_id = "location";
     }else if (permission == "notifications") {
       table = "notifications";
       resource_id = "notifications";
     } else if (permission == "background") {
       table = "background";
       resource_id = app_id;
     }
     GetAllPermissions(table,resource_id,app_id,[results,callback,counter,total,permission](PermissionStatus status) {
       (*results)[permission] = status;
         if(++(*counter) == total) {
           callback(*results);
         }
     });
   }
}

void AccessPortal::GetPermission(const std::string& table, const std::string& id, const std::string& app, std::function<void(PermissionStatus status, std::vector<std::string> permissions)> callback) {
   std::vector<std::string> permissions;
   PortalInterface access_interface;
   access_interface.service_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.object_path = "/org/freedesktop/impl/portal/PermissionStore";
   access_interface.interface_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.bus_type = BUS_TYPE::SESSION;
   auto proxy = portal_manager_.GetPortalProxy().GetProxy(access_interface);

   proxy->callMethodAsync("GetPermission")
      .onInterface("org.freedesktop.impl.portal.PermissionStore")
      .withArguments(table, id, app)
      .uponReplyInvoke([callback,table,id,app](std::optional<sdbus::Error> error,
                                        const std::vector<std::string>& permissions) {
          if (error) {
            const std::string error_msg = error->getMessage();
            if (error_msg.find("No entry") != std::string::npos ||
                    error_msg.find("not found") != std::string::npos ||
                    error->getName() == "org.freedesktop.DBus.Error.UnknownMethod" ||
                    error->getName() == "org.freedesktop.portal.Error.NotFound") {
              spdlog::debug("[AccessPortal] Permission not set for {}/{}/{}",
                                 table, id, app);
              callback(PermissionStatus::NOT_SET,{});
              return;
                    }
              spdlog::error("[AccessPortal] GetPermission failed: {} ({})", error->getMessage(),error->getName());
              callback(PermissionStatus::NOT_SET,{});
            return;
          }

        // success to retrieve
        if (permissions.empty()) {
                callback(PermissionStatus::NOT_SET, {});
            } else if (permissions[0] == "yes") {
                callback(PermissionStatus::GRANTED, permissions);
            } else if (permissions[0] == "no") {
                callback(PermissionStatus::DENIED, permissions);
            } else if (permissions[0] == "ask") {
                callback(PermissionStatus::ASK, permissions);
            } else {
                spdlog::warn("[AccessPortal] Unknown permission value: {}",
                            permissions[0]);
                callback(PermissionStatus::NOT_SET, permissions);
            }
      });
 }

void AccessPortal::SetPermission(
    const std::string& table,
    const std::string& id,
    const std::string& app,
    const std::vector<std::string>& permission,
    std::function<void(bool ready)> callback) {
   PortalInterface access_interface;
   access_interface.service_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.object_path = "/org/freedesktop/impl/portal/PermissionStore";
   access_interface.interface_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.bus_type = BUS_TYPE::SESSION;
   auto proxy = portal_manager_.GetPortalProxy().GetProxy(access_interface);

   proxy->callMethodAsync("SetPermission")
      .onInterface("org.freedesktop.impl.portal.PermissionStore")
      .withArguments(table,true,id, app,permission)
      .uponReplyInvoke([callback](std::optional<sdbus::Error> error) {
        if (error) {
          spdlog::error("[AccessPortal] SetPermission failed: {}", error->getMessage());
          callback(false);
        } else {
          spdlog::debug("[AccessPortal] SetPermission successful");
          callback(true);
        }
      });
}

void AccessPortal::DeletePermission(const std::string& table,
                                    const std::string& id,
                                    const std::string& app,
                                    std::function<void(bool ready)> callback) {
   PortalInterface access_interface;
   access_interface.service_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.object_path = "/org/freedesktop/impl/portal/PermissionStore";
   access_interface.interface_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.bus_type = BUS_TYPE::SESSION;
   auto proxy = portal_manager_.GetPortalProxy().GetProxy(access_interface);

   proxy->callMethodAsync("DeletePermission")
      .onInterface("org.freedesktop.impl.portal.PermissionStore")
      .withArguments(table, id, app)
      .uponReplyInvoke([callback](std::optional<sdbus::Error> error) {
          if (error) {
              spdlog::error("[AccessPortal] DeletePermission failed: {}", error->getMessage());
              callback(false);
          } else {
              spdlog::debug("DeletePermission successful");
              callback(true);
          }
      });
}

void AccessPortal::SetResource(
    const std::string& table,
    const std::string& id,
    const std::map<std::string, std::vector<std::string>>& permissions,
    std::function<void(bool ready)> callback) {
   auto now = std::chrono::system_clock::now();
   auto now_c = std::chrono::system_clock::to_time_t(now);
   std::ostringstream oss;
   oss << std::put_time(std::localtime(&now_c), "%F %T");
   std::string log = oss.str();

   PortalInterface access_interface;
   access_interface.service_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.object_path = "/org/freedesktop/impl/portal/PermissionStore";
   access_interface.interface_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.bus_type = BUS_TYPE::SESSION;
   auto proxy = portal_manager_.GetPortalProxy().GetProxy(access_interface);

   proxy->callMethodAsync("Set")
      .onInterface("org.freedesktop.impl.portal.PermissionStore")
      .withArguments(table, true, id, permissions, log)
      .uponReplyInvoke([callback](std::optional<sdbus::Error> error) {
          if (error) {
              spdlog::error("[AccessPortal] Set Resource failed: {}", error->getMessage());
              callback(false);
          } else {
              spdlog::debug("Set Resource successfully");
              callback(true);
          }
      });
 }

void AccessPortal::DeleteResource(const std::string& table,
                                  const std::string& id,
                                  std::function<void(bool ready)> callback) {
   PortalInterface access_interface;
   access_interface.service_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.object_path = "/org/freedesktop/impl/portal/PermissionStore";
   access_interface.interface_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.bus_type = BUS_TYPE::SESSION;
   auto proxy = portal_manager_.GetPortalProxy().GetProxy(access_interface);

   proxy->callMethodAsync("Delete")
      .onInterface("org.freedesktop.impl.portal.PermissionStore")
      .withArguments(table,id)
      .uponReplyInvoke([callback](std::optional<sdbus::Error> error) {
          if (error) {
              spdlog::error("[AccessPortal] Set Resource failed: {}", error->getMessage());
              callback(false);
          } else {
              spdlog::debug("Delete Resource successfully");
              callback(true);
          }
      });
}
void AccessPortal::GetAllResource(
    const std::string& table,
     std::function<void(bool success, std::vector<std::string> resources)> callback) {
   PortalInterface access_interface;
   access_interface.service_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.object_path = "/org/freedesktop/impl/portal/PermissionStore";
   access_interface.interface_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.bus_type = BUS_TYPE::SESSION;
   auto proxy = portal_manager_.GetPortalProxy().GetProxy(access_interface);

   proxy->callMethodAsync("List")
      .onInterface("org.freedesktop.impl.portal.PermissionStore")
      .withArguments(table)
      .uponReplyInvoke([callback](std::optional<sdbus::Error> error,const std::vector<std::string>& resources) {
          if (error) {
              spdlog::error("[AccessPortal] Get all Resource failed: {}", error->getMessage());
              callback(false,resources);
          } else {
              spdlog::debug("Get all Resource successfully fetched {} resource",resources.size());
              callback(true,resources);
          }
      });
}

void AccessPortal::GetAllPermissions(
    const std::string& table,
    const std::string& id,
    const std::string& app_id,
    std::function<void(PermissionStatus status)> callback) {
   PortalInterface access_interface;
   access_interface.service_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.object_path = "/org/freedesktop/impl/portal/PermissionStore";
   access_interface.interface_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.bus_type = BUS_TYPE::SESSION;
   auto proxy = portal_manager_.GetPortalProxy().GetProxy(access_interface);

   proxy->callMethodAsync("Lookup")
      .onInterface("org.freedesktop.impl.portal.PermissionStore")
      .withArguments(table, id)
      .uponReplyInvoke([callback,app_id](std::optional<sdbus::Error> error,
                                        const std::map<std::string, std::vector<std::string>>& permissions,const sdbus::Variant& data) {
          if (error) {
            if (error->getMessage().find("No table") != std::string::npos ||
                  error->getMessage().find("not found") != std::string::npos) {
                  spdlog::debug("[AccessPortal] Table/resource not found");
                  callback(PermissionStatus::NOT_SET);
                  return;
              }
              spdlog::error("[AccessPortal] Get All Permissions with lookup failed: {}", error->getMessage());
              callback(PermissionStatus::NOT_SET);
            return;
          }
        auto item = permissions.find(app_id);
        if (item == permissions.end() || item->second.empty()) {
          callback(PermissionStatus::NOT_SET);
          return;
        }
        const std::string& perm = item->second[0];
        if (perm == "yes") {
          callback(PermissionStatus::GRANTED);
        }else if (perm == "no") {
          callback(PermissionStatus::DENIED);
        }else if (perm == "ask") {
          callback(PermissionStatus::ASK);
        }else {
          callback(PermissionStatus::NOT_SET);
        }
      });
}

void AccessPortal::StoreTimestamp(const std::string& table,
                                  const std::string& id,
                                  const std::string& data,
                                  std::function<void(bool ready)> callback) {
   PortalInterface access_interface;
   access_interface.service_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.object_path = "/org/freedesktop/impl/portal/PermissionStore";
   access_interface.interface_name = "org.freedesktop.impl.portal.PermissionStore";
   access_interface.bus_type = BUS_TYPE::SESSION;
   auto proxy = portal_manager_.GetPortalProxy().GetProxy(access_interface);

   proxy->callMethodAsync("SetValue")
      .onInterface("org.freedesktop.impl.portal.PermissionStore")
      .withArguments(table,true,id, data)
      .uponReplyInvoke([callback](std::optional<sdbus::Error> error) {
        if (error) {
          spdlog::error("[AccessPortal] SetPermission failed: {}", error->getMessage());
          callback(false);
        } else {
          spdlog::debug("[AccessPortal] SetPermission successful");
          callback(true);
        }
      });
}

}  // namespace flatpak_plugin