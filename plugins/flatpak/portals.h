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

#ifndef PORTALS_H
#define PORTALS_H

#include <gio/gio.h>

#include <asio/io_context_strand.hpp>
#include <mutex>
#include <string>

namespace flatpak_plugin {
/**
 * \brief A utility class providing helper functions to Manage
 * XDG Desktop Portal lifecycle and D-Bus connections.
 */
class Portals {
 public:
  explicit Portals(asio::io_context& io_context);
  ~Portals();

  Portals(const Portals&) = delete;
  Portals& operator=(const Portals&) = delete;
  Portals(Portals&&) = delete;
  Portals& operator=(Portals&&) = delete;

  using ServiceCallback =
      std::function<void(bool success, const std::string& service)>;
  using ReadyCallback = std::function<void(bool ready)>;

  /**
   * \brief Initializes XDG Desktop Portals.
   * \param strand Execution context for async operations.
   * \param callback Called on strand thread with success/failure.
   */
  void initialize_portals(const asio::io_context::strand& strand,
                          std::function<void(bool)> callback);

  /**
   * \brief Sets up D-Bus policy (session/system bus permissions).
   * \param strand Execution context for async operations.
   * \param portals Vector of all portals needed to run App.
   * \param callback Called on strand thread with success/failure.
   */
  void setup_portals(asio::io_context::strand& strand,
                     const std::vector<std::string>& portals,
                     ReadyCallback callback);

  /**
   * \brief Checks up D-Bus portal if it's available or not.
   * \param portal_id id of a portal to check.
   */
  bool portal_available(const std::string& portal_id) const;

  /**
   * \brief Create a d-bus permission dialog to grantee access to app.
   * \param app Application id for the application who need permission.
   * \param permissions Vector of permissions needed for the application.
   */
  bool create_dialog(const std::string& app,
                     const std::vector<std::string>& permissions);

  /**
   * \brief Check for permission if it is stored by portal or not.
   * \param strand Asio object passed to function to execute post-function
   * code on the same thread.
   * \param app id of the Application.
   * \param permissions Vector of strings contains all permissions needed.
   * \param callback Called on strand thread with success/failure.
   */
  void check_permissions(asio::io_context::strand& strand,
                         const std::string& app,
                         const std::vector<std::string>& permissions,
                         ReadyCallback callback);

  /**
   * \brief Store permissions for Application to never ask for it again.
   * \param strand Asio object passed to function to execute post-function
   * code on the same thread.
   * \param app id of the Application.
   * \param permissions Vector of strings contains all permissions needed.
   * \param granted Checks if permission is granted for app or not.
   * \param callback Called on strand thread with success/failure.
   */
  void store_permissions(asio::io_context::strand& strand,
                         const std::string& app,
                         const std::vector<std::string>& permissions,
                         bool granted,
                         ReadyCallback callback);

  [[nodiscard]] bool portal_ready() const { return ready_.load(); }

  [[nodiscard]] GDBusConnection* get_session_bus() const { return session_; }

 private:
  asio::io_context& io_context_;
  GDBusConnection* session_;
  GDBusConnection* system_;
  mutable std::mutex mutex_;
  std::atomic<bool> ready_;
  std::atomic<bool> shutting_down_;
  std::unordered_map<std::string, bool> portals_status_;

  void run_portal(const asio::io_context::strand& strand,
                  const std::string& portal,
                  ServiceCallback callback) const;

  void setup_portal(asio::io_context::strand& strand,
                    const std::string& portal,
                    ServiceCallback callback) const;
};
};  // namespace flatpak_plugin

#endif  // PORTALS_H
