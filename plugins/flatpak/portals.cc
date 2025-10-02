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

#include "portals.h"

#include <gio/gdbusconnection.h>
#include <gio/gdbusproxy.h>
#include <gtk/gtk.h>

#include "asio/post.hpp"
#include "spdlog/spdlog.h"

namespace flatpak_plugin {

Portals::Portals(asio::io_context& io_context)
    : io_context_(io_context),
      session_(nullptr),
      system_(nullptr),
      ready_(false),
      shutting_down_(false) {}

Portals::~Portals() {
  shutting_down_.store(true);
  std::lock_guard<std::mutex> lock(mutex_);
  if (session_) {
    g_object_unref(session_);
    session_ = nullptr;
  }
  if (system_) {
    g_object_unref(system_);
    system_ = nullptr;
  }
}

void Portals::initialize_portals(const asio::io_context::strand& strand,
                                 std::function<void(bool)> callback) {
  asio::post(strand, [this, callback = std::move(callback)]() {
    GError* error = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      session_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
      if (error) {
        spdlog::error("[Flatpak Plugin] Failed to setup portal session bus: {}",
                      error->message);
        g_clear_error(&error);
      }
      system_ = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
      if (error) {
        spdlog::error("[Flatpak Plugin] Failed to setup portal system bus: {}",
                      error->message);
        g_clear_error(&error);
      }
      gtk_init(nullptr, nullptr);
    }
    // add portal configs gnome first, fallback to GTK
    const char* config_dir = g_get_user_config_dir();
    std::string portal_config_dir =
        std::string(config_dir) + "/xdg-desktop-portal";
    std::string portal_config_file =
        std::string(portal_config_dir) + "/portals.conf";

    if (g_mkdir_with_parents(portal_config_dir.c_str(), 0755) != 0) {
      spdlog::error("[Flatpak Plugin] Failed to create portals directory {}",
                    portal_config_dir);
    }
    if (g_file_test(portal_config_file.c_str(), G_FILE_TEST_EXISTS)) {
      spdlog::debug("[Flatpak Plugin] Port file already exists");
    }
    const char* portal_config =
        "[preferred]\n"
        "default=gnome;gtk\n"
        "\n"
        "[org.freedesktop.impl.portal.FileChooser]\n"
        "default=gnome;gtk\n"
        "\n"
        "[org.freedesktop.impl.portal.Notification]\n"
        "default=gnome;gtk\n"
        "\n"
        "[org.freedesktop.impl.portal.Settings]\n"
        "default=gnome;gtk\n";
    g_file_set_contents(portal_config_file.c_str(), portal_config, -1, &error);
    if (error) {
      g_clear_error(&error);
    }
    spdlog::debug("[Flatpak Plugin] Portals initialized");
    callback(true);
  });
}

bool Portals::portal_available(const std::string& portal_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!session_) {
    return false;
  }

  GError* error = nullptr;
  GDBusProxy* proxy = g_dbus_proxy_new_sync(
      session_, G_DBUS_PROXY_FLAGS_NONE, nullptr, portal_id.c_str(),
      "/org/freedesktop/portal/desktop", "org.freedesktop.DBus.Properties",
      nullptr, &error);
  if (error) {
    spdlog::error("[Flatpak Plugin] Failed to create portal proxy: {}",
                  error->message);
    g_clear_error(&error);
    return false;
  }
  if (proxy) {
    g_object_unref(proxy);
    spdlog::debug("[Flatpak Plugin] Portal found at {}", portal_id);
  }
  return proxy != nullptr;
}

void Portals::setup_portals(asio::io_context::strand& strand,
                            const std::vector<std::string>& portals,
                            ReadyCallback callback) {
  if (portals.empty()) {
    asio::post(strand,
               [this, callback = std::move(callback)]() { callback(true); });
    return;
  }
  spdlog::debug("[Flatpak Plugin] Setup {} portals", portals.size());
  std::vector<std::string> all_portals = {"org.freedesktop.portal.Desktop",
                                          "org.freedesktop.portal.Documents"};

  for (const auto& portal : portals) {
    if (std::find(all_portals.begin(), all_portals.end(), portal) ==
        all_portals.end()) {
      all_portals.push_back(portal);
    }
  }

  // manage portals lifecycle
  auto portals_states =
      std::make_shared<std::unordered_map<std::string, bool>>();
  auto done = std::make_shared<std::atomic<size_t>>(0);
  auto total_portals = all_portals.size();

  for (const auto& portal : all_portals) {
    (*portals_states)[portal] = false;
    setup_portal(
        strand, portal,
        [this, portals_states, done, total_portals, callback](
            bool success, const std::string& portal_id) {
          (*portals_states)[portal_id] = success;
          size_t completed_portals = done->fetch_add(1) + 1;
          if (completed_portals >= total_portals) {
            bool all_ready =
                std::all_of(portals_states->begin(), portals_states->end(),
                            [](const auto& pair) { return pair.second; });
            callback(all_ready);
          }
          if (success) {
            spdlog::debug("[Flatpak Plugin] Portal ready :{}", portal_id);
          } else {
            spdlog::error("[Flatpak Plugin] Portal not ready :{}", portal_id);
          }
        });
  }
}

void Portals::run_portal(const asio::io_context::strand& strand,
                         const std::string& portal,
                         ServiceCallback callback) const {
  asio::post(strand, [this, portal, callback = std::move(callback)] {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_) {
      callback(false, portal);
      return;
    }
    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        session_, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "StartServiceByName",
        g_variant_new("(su)", portal.c_str(), 0), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
    if (error) {
      spdlog::error("[Flatpak Plugin] Failed to setup portal service: {}",
                    error->message);
      g_clear_error(&error);
    }
    if (result) {
      guint32 code;
      g_variant_get(result, "(u)", &code);
      auto success = (code == 1 || code == 2);
      if (success) {
        spdlog::debug("[Flatpak Plugin] Portal started at {}", portal);
      } else {
        spdlog::error("[Flatpak Plugin] Failed to start portal service: {}",
                      error->message);
      }
      g_variant_unref(result);
      callback(success, portal);
    } else {
      callback(false, portal);
    }
  });
}

void Portals::setup_portal(asio::io_context::strand& strand,
                           const std::string& portal,
                           ServiceCallback callback) const {
  asio::post(strand, [this, strand, portal,
                      callback = std::move(callback)]() mutable {
    if (portal_available(portal)) {
      spdlog::debug("[Flatpak Plugin] Portal already available: {}", portal);
      callback(true, portal);
      return;
    }
    spdlog::debug("[Flatpak Plugin] Running Portal {}", portal);
    run_portal(strand, portal,
               [this, strand, portal, callback](
                   bool active, const std::string& name) mutable {
                 callback(active, name);
               });
  });
}

bool Portals::create_dialog(const std::string& app,
                            const std::vector<std::string>& permissions) {
  std::string text = app + " requests:\n\n";
  for (const auto& permission : permissions) {
    text += "• " + permission + "\n";
  }

  text += "\nAllow These Permissions?";
  GtkWidget* dialog =
      gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                             GTK_BUTTONS_YES_NO, "%s", text.c_str());
  gtk_window_set_title(GTK_WINDOW(dialog), (app + " Permissions").c_str());
  gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);

  gint response = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  while (gtk_events_pending()) {
    gtk_main_iteration();
  }
  bool success = (response == GTK_RESPONSE_YES);
  spdlog::info("[Flatpak Plugin] User response for {} : {}", app,
               success ? "Allowed" : "Not Allowed");
  return success;
}

void Portals::check_permissions(asio::io_context::strand& strand,
                                const std::string& app,
                                const std::vector<std::string>& permissions,
                                ReadyCallback callback) {
  asio::post(strand, [this, app, permissions,
                      callback = std::move(callback)]() mutable {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_) {
      callback(false);
      return;
    }
    GError* error = nullptr;
    GDBusProxy* proxy = g_dbus_proxy_new_sync(
        session_, G_DBUS_PROXY_FLAGS_NONE, nullptr,
        "org.freedesktop.impl.portal.PermissionStore",
        "/org/freedesktop/impl/portal/PermissionStore",
        "org.freedesktop.impl.portal.PermissionStore", nullptr, &error);

    if (error) {
      spdlog::debug("[Flatpak Plugin] Failed to setup permission store: {}",
                    error->message);
      g_clear_error(&error);
      if (proxy) {
        g_object_unref(proxy);
      }
      callback(false);
      return;
    }
    GVariant* result = g_dbus_proxy_call_sync(
        proxy, "Lookup", g_variant_new("(ss)", "ivi", app.c_str()),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    g_object_unref(proxy);
    if (error) {
      g_clear_error(&error);
      callback(false);
      return;
    }
    bool all_granted = true;

    // parse stored permissions
    GVariant* dictionary;
    GVariant* data;
    g_variant_get(result, "(@a{sas}@v)", &dictionary, &data);
    for (const auto& perm : permissions) {
      GVariant* perm_value = g_variant_lookup_value(dictionary, perm.c_str(),
                                                    G_VARIANT_TYPE("as"));

      if (!perm_value || g_variant_n_children(perm_value) == 0) {
        all_granted = false;
        if (perm_value)
          g_variant_unref(perm_value);
        break;
      }
      g_variant_unref(perm_value);
    }

    g_variant_unref(dictionary);
    g_variant_unref(data);
    g_variant_unref(result);
    spdlog::info("[Flatpak Plugin] Stored permissions for {}: {}", app,
                 all_granted ? "GRANTED" : "MISSING");

    callback(all_granted);
  });
}

void Portals::store_permissions(asio::io_context::strand& strand,
                                const std::string& app,
                                const std::vector<std::string>& permissions,
                                bool granted,
                                ReadyCallback callback) {
  asio::post(strand, [this, app, permissions, callback = std::move(callback),
                      granted]() mutable {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_) {
      callback(false);
      return;
    }
    GError* error = nullptr;
    GDBusProxy* proxy = g_dbus_proxy_new_sync(
        session_, G_DBUS_PROXY_FLAGS_NONE, nullptr,
        "org.freedesktop.impl.portal.PermissionStore",
        "/org/freedesktop/impl/portal/PermissionStore",
        "org.freedesktop.impl.portal.PermissionStore", nullptr, &error);

    if (error) {
      spdlog::error("[Flatpak Plugin] Failed to store permissions: {}",
                    error->message);
      g_clear_error(&error);
      callback(false);
      return;
    }
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sas}"));

    for (const auto& perm : permissions) {
      GVariantBuilder builder_val;
      g_variant_builder_init(&builder_val, G_VARIANT_TYPE("as"));
      if (granted) {
        g_variant_builder_add(&builder_val, "s", "yes");
      }
      g_variant_builder_add(&builder, "{s@as}", perm.c_str(),
                            g_variant_builder_end(&builder_val));
    }
    GVariant* permissions_variant = g_variant_builder_end(&builder);
    GVariantBuilder data;
    g_variant_builder_init(&data, G_VARIANT_TYPE("a{sv}"));
    GVariant* data_variant = g_variant_builder_end(&data);
    GVariant* result = g_dbus_proxy_call_sync(
        proxy, "Set",
        g_variant_new("(sbs@a{sas}@v)", "ivi", TRUE, app.c_str(),
                      permissions_variant, g_variant_new_variant(data_variant)),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    g_object_unref(proxy);
    if (error) {
      spdlog::error("[Flatpak Plugin] Failed to store permissions: {}",
                    error->message);
      g_clear_error(&error);
      callback(false);
      return;
    }
    if (result) {
      g_variant_unref(result);
    }
    spdlog::info("[Flatpak Plugin] Stored permissions for {}: {}", app,
                 granted ? "GRANTED" : "MISSING");
    callback(true);
  });
}

}  // namespace flatpak_plugin
