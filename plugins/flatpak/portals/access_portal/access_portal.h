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

#ifndef IVI_HOMESCREEN_ACCESS_PORTAL_H
#define IVI_HOMESCREEN_ACCESS_PORTAL_H

#include <string>

#include <flutter/event_channel.h>
#include "flatpak/messages.g.h"
#include "flatpak/portals/portal_manager.h"
#include "plugins/common/sdbus/sdbus.h"

namespace flatpak_plugin {
struct AccessDialogOptions {
  std::string title;
  std::string icon;
  bool modal = true;
};

struct AccessRequest {
  std::string handle;
  std::string app_id;
  AccessDialogOptions options;
  std::unique_ptr<sdbus::IProxy> request_proxy;
  std::function<void(uint32_t,std::map<std::string,sdbus::Variant>)> callback;
};

class AccessPortal {
public:
  enum class PermissionStatus {
    GRANTED,      // "yes"
    DENIED,       // "no"
    ASK,          // "ask"
    NOT_SET       // No entry exists
  };
  explicit AccessPortal(PortalManager* portal_manager,asio::io_context& io_context,flutter::EventChannel<flutter::EncodableValue>& event_channel);

  ~AccessPortal();


  void CheckAllPermissions(const std::string& app_id,const std::vector<std::string>& permissions,std::function<void(std::map<std::string,PermissionStatus>)> callback);
  /**
   * \brief Sets permission for app.
   * \param table Table of resources for apps which could be either a device or a feature.
   * \param id Identifier for the resource in the table.
   * \param app Application that needs access to a resource.
   * \param permission Permission assigned to app could be 'yes','no' or 'ask'.
   * \param callback Callback function for async operations.
   * \return String of Stored permission.
   */
  void SetPermission(const std::string& table,const std::string& id,const std::string& app,const std::vector<std::string>& permission,std::function<void(bool ready)> callback);

  /**
   * \brief Retrieves permission for app.
   * \param table Table of resources for apps which could be either a device or a feature.
   * \param id Identifier for the resource in the table.
   * \param app Application that needs access to a resource.
   * \param callback Callback function for async operations.
   * \return String of Stored permission.
   */
  void GetPermission(const std::string& table,const std::string& id,const std::string& app,std::function<void(PermissionStatus status, std::vector<std::string> permissions)> callback);

  /**
   * \brief Delete permission for app.
   * \param table Table of resources for apps which could be either a device or a feature.
   * \param id Identifier for the resource in the table.
   * \param app Application that needs access to a resource.
   * \param ready_callback Callback function for async operations.
   * \return String of Stored permission.
   */
  void DeletePermission(const std::string& table,const std::string& id,const std::string& app,std::function<void(bool ready)> callback);

  /**
   * \brief Retrieves all permissions for a specific resource.
   * \param table Table of resources for apps which could be either a device or a feature.
   * \param id Identifier for the resource in the table.
   * \param callback Callback function for async operations.
   * \return Map of Stored permissions to a resource.
   */
  void GetAllPermissions(
    const std::string& table,
    const std::string& id,
    const std::string& app_id,
    std::function<void(PermissionStatus status)> callback);

  /**
   * \brief Sets an entire resource entry with multiple app permissions at once.
   * \param table Table of resources for apps which could be either a device or a feature.
   * \param id Identifier for the resource in the table.
   * \param permissions Permissions assigned to apps in a resource could be 'yes','no' or 'ask'.
   * \param callback Callback function for async operations.
   */
  void SetResource(const std::string& table,const std::string& id,const std::map<std::string, std::vector<std::string>>& permissions,std::function<void(bool ready)> callback);

  /**
   * \brief Delete an entire resource and ALL associated app permissions.
   * \param table Table of resources for apps which could be either a device or a feature.
   * \param id Identifier for the resource in the table.
   * \param ready_callback Callback function for async operations.
   */
  void DeleteResource(const std::string& table,const std::string& id,std::function<void(bool ready)> callback);

  /**
   * \brief Get all device types that have any permissions set.
   * \param table Table of resources for apps which could be either a device or a feature.
   * \param callback Callback function for async operations.
   */
  void GetAllResource(const std::string& table, std::function<void(bool success, std::vector<std::string> resources)> callback);

  /**
   * \brief Storing timestamps of when a resource was last accessed.
   * \param table Table of resources for apps which could be either a device or a feature.
   * \param id Identifier for the resource in the table.
   * \param timestamp A human-readable descriptions, or storing state information that multiple apps might need to know about the resource.
   * \param callback Callback function for async operations.
   */
  void StoreTimestamp(const std::string& table,const std::string& id,const std::string& data,std::function<void(bool ready)> callback);

  std::string ShowAccessDialog(const std::string& app_id,const std::string& parent_window,const AccessDialogOptions& options);

  void CancelAccessRequest(const std::string& handle);

private:
  std::string GenerateHandle(const std::string& app_id);

  void SetupRequestSignalHandler(const std::string& handler);

  void OnPortalResponse(const std::string& handle, uint32_t response_code, const std::map<std::string,sdbus::Variant>& results);

  void SendEvent(const flutter::EncodableMap& event);

  PortalManager& portal_manager_;
  asio::io_context& io_context_;
  //std::shared_ptr<FlatpakShim> shim_;
  flutter::EventChannel<flutter::EncodableValue>* event_channel_;
  std::unordered_map<std::string,std::unique_ptr<AccessRequest>> requests_;
  std::mutex requests_mutex_;
  std::atomic<uint32_t> request_counter_{0};
  plugin_common_sdbus::SessionDBus& session_bus_;
};
}
#endif  // IVI_HOMESCREEN_ACCESS_PORTAL_H