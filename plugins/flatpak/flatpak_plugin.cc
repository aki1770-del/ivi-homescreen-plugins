/*
 * Copyright 2020-2024 Toyota Connected North America
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

#include "flatpak_plugin.h"

#include <filesystem>
#include <sstream>
#include <vector>

#include <zlib.h>
#include <asio/dispatch.hpp>
#include <asio/post.hpp>
#include <future>

#include <flutter/plugin_registrar_homescreen.h>
#include "messages.g.h"
#include "plugins/common/common.h"
#include "plugins/common/glib/main_loop.h"

namespace flatpak_plugin {

// static
void FlatpakPlugin::RegisterWithRegistrar(flutter::PluginRegistrar* registrar) {
  auto plugin = std::make_unique<FlatpakPlugin>(registrar);

  SetUp(registrar->messenger(), plugin.get());

  plugin->Init();

  registrar->AddPlugin(std::move(plugin));
}

FlatpakPlugin::FlatpakPlugin(flutter::PluginRegistrar* registrar)
    : io_context_(std::make_unique<asio::io_context>(ASIO_CONCURRENCY_HINT_1)),
      work_(io_context_->get_executor()),
      strand_(std::make_unique<asio::io_context::strand>(*io_context_)),
      registrar_(registrar) {
  plugin_common_glib::MainLoop::GetInstance();
  spdlog::info("[FlatpakPlugin] GLIB Main loop initialized");
  thread_ = std::thread([&] { io_context_->run(); });

  asio::post(*strand_, [&]() {
    pthread_self_ = pthread_self();
    spdlog::debug("\tthread_id=0x{:x}", pthread_self_);
  });

  spdlog::debug("[FlatpakPlugin]");
  spdlog::debug("\tlinked with libflatpak.so v{}.{}.{}", FLATPAK_MAJOR_VERSION,
                FLATPAK_MINOR_VERSION, FLATPAK_MICRO_VERSION);
  spdlog::debug("\tDefault Arch: {}", flatpak_get_default_arch());
  spdlog::debug("\tSupported Arches:");
  if (auto* supported_arches = flatpak_get_supported_arches()) {
    for (auto arch = supported_arches; *arch != nullptr; ++arch) {
      spdlog::debug("\t\t{}", *arch);
    }
  }

  shim_ = std::make_shared<FlatpakShim>(this, registrar_->messenger(),
                                        strand_.get());
  portal_manager_ = std::make_shared<PortalManager>(*io_context_);
  cache_manager_ = CacheManager::Builder()
                       .WithDatabasePath("/tmp/flatpak_plugin.db")
                       .WithCachePolicy(CachePolicy::CACHE_FIRST)
                       .WithAutoCleanup(true, std::chrono::minutes(5))
                       .WithDefaultTTL(std::chrono::minutes(10))
                       .WithCompression(false)
                       .WithMaxCacheSize(50)
                       .WithMaxRetries(3)
                       .WithNetworkTimeout(std::chrono::seconds(5))
                       .WithMetrics(true)
                       .Build();
}

FlatpakPlugin::~FlatpakPlugin() {
  io_context_->stop();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void FlatpakPlugin::Init() {
  shim_-> SetupTransactionEventChannel(registrar_->messenger());
  spdlog::info("[FlatpakPlugin] Event channel Setup Complete");
}

// Get Flatpak Version
ErrorOr<std::string> FlatpakPlugin::GetVersion() {
  std::stringstream ss;
  ss << FLATPAK_MAJOR_VERSION << "." << FLATPAK_MINOR_VERSION << "."
     << FLATPAK_MICRO_VERSION;
  return ErrorOr<std::string>(ss.str());
}

ErrorOr<flutter::EncodableList> FlatpakPlugin::GetRemotesByInstallationId(
    const std::string& installation_id) {
  return FlatpakShim::get_remotes_by_installation_id(installation_id);
}

// Get the default flatpak arch
ErrorOr<std::string> FlatpakPlugin::GetDefaultArch() {
  std::string default_arch = flatpak_get_default_arch();
  return ErrorOr(std::move(default_arch));
}

// Get all arches supported by flatpak
ErrorOr<flutter::EncodableList> FlatpakPlugin::GetSupportedArches() {
  flutter::EncodableList result;
  if (auto* supported_arches = flatpak_get_supported_arches()) {
    for (auto arch = supported_arches; *arch != nullptr; ++arch) {
      result.emplace_back(static_cast<const char*>(*arch));
    }
  }
  return ErrorOr(std::move(result));
}

// Get configuration of user installation.
ErrorOr<Installation> FlatpakPlugin::GetUserInstallation() {
  auto result = cache_manager_->GetUserInstallation(false);
  return ErrorOr<Installation>(std::move(result.value()));
}

ErrorOr<flutter::EncodableList> FlatpakPlugin::GetSystemInstallations() {
  auto result = cache_manager_->GetSystemInstallations(false);
  if (!result.has_value()) {
    return ErrorOr<flutter::EncodableList>(flutter::EncodableList());
  }
  return ErrorOr<flutter::EncodableList>(std::move(result.value()));
}

ErrorOr<bool> FlatpakPlugin::RemoteAdd(const Remote& configuration) {
  return FlatpakShim::RemoteAdd(configuration);
}

ErrorOr<bool> FlatpakPlugin::RemoteRemove(const std::string& id) {
  return FlatpakShim::RemoteRemove(id);
}

ErrorOr<flutter::EncodableList> FlatpakPlugin::GetApplicationsInstalled() {
  auto result = cache_manager_->GetApplicationsInstalled(false);
  if (!result.has_value()) {
    return ErrorOr<flutter::EncodableList>(flutter::EncodableList());
  }
  return ErrorOr<flutter::EncodableList>(std::move(result.value()));
}

ErrorOr<flutter::EncodableList> FlatpakPlugin::GetApplicationsUpdate() {
  const auto result = FlatpakShim::GetApplicationsUpdate();
  if (result.has_error()) {
    return ErrorOr<flutter::EncodableList>(flutter::EncodableList());
  }
  return ErrorOr<flutter::EncodableList>(result.value());
}

ErrorOr<flutter::EncodableList> FlatpakPlugin::GetApplicationsRemote(
    const std::string& id) {
  auto result = cache_manager_->GetApplicationsRemote(id, false);
  if (!result.has_value()) {
    return ErrorOr<flutter::EncodableList>(flutter::EncodableList());
  }
  return ErrorOr<flutter::EncodableList>(std::move(result.value()));
}

void FlatpakPlugin::ApplicationInstall(
    const std::string& id,
    std::function<void(ErrorOr<bool> reply)> result) {
  auto shim = std::make_shared<FlatpakShim>(this, registrar_->messenger(),
                                            strand_.get());

  struct PromiseGuard {
    std::function<void(ErrorOr<bool>)> callback;
    std::once_flag flag;

    void call(ErrorOr<bool> error) {
      std::call_once(flag, [this, error = std::move(error)]() mutable {
        try {
          callback(std::move(error));
        } catch (const std::exception& e) {
          spdlog::error("Exception in ApplicationInstall callback: {}",
                        e.what());
        } catch (...) {
          spdlog::error("Unknown exception in ApplicationInstall callback");
        }
      });
    }
  };

  auto guard = std::make_shared<PromiseGuard>();
  guard->callback = std::move(result);

  asio::dispatch(*strand_, [this, id, guard, shim]() mutable {
    shim->SetupTransactionEventChannel(registrar_->messenger());

    shim->ApplicationInstall(id, [guard](const ErrorOr<bool>& install_result) {
      if (install_result.has_error()) {
        FlutterError error("INSTALL_FAILED", install_result.error().message(),
                           flutter::EncodableValue());
        guard->call(ErrorOr<bool>(false));
      } else if (install_result.value()) {
        guard->call(ErrorOr<bool>(true));
      } else {
        FlutterError error("INSTALL_FAILED",
                           "Installation returned false without error",
                           flutter::EncodableValue());
        guard->call(ErrorOr<bool>(false));
      }
    });
  });
}

void FlatpakPlugin::ApplicationUninstall(
    const std::string& id,
    std::function<void(ErrorOr<bool> reply)> result) {
  auto shim = std::make_shared<FlatpakShim>(this, registrar_->messenger(),
                                            strand_.get());

  struct PromiseGuard {
    std::function<void(ErrorOr<bool>)> callback;
    std::once_flag flag;

    void call(ErrorOr<bool> error) {
      std::call_once(flag, [this, error = std::move(error)]() mutable {
        try {
          callback(std::move(error));
        } catch (const std::exception& e) {
          spdlog::error("Exception in ApplicationUninstall callback: {}",
                        e.what());
        } catch (...) {
          spdlog::error("Unknown exception in ApplicationUninstall callback");
        }
      });
    }
  };

  auto guard = std::make_shared<PromiseGuard>();
  guard->callback = std::move(result);

  asio::dispatch(*strand_, [this, id, guard, shim]() mutable {
    shim->SetupTransactionEventChannel(registrar_->messenger());

    shim->ApplicationUninstall(
        id, [guard](const ErrorOr<bool>& uninstall_result) {
          if (uninstall_result.has_error()) {
            FlutterError error("UNINSTALL_FAILED",
                               uninstall_result.error().message(),
                               flutter::EncodableValue());
            guard->call(ErrorOr<bool>(false));
          } else if (uninstall_result.value()) {
            guard->call(ErrorOr<bool>(true));
          } else {
            FlutterError error("UNINSTALL_FAILED",
                               "Uninstallation returned false without error",
                               flutter::EncodableValue());
            guard->call(ErrorOr<bool>(false));
          }
        });
  });
}

void FlatpakPlugin::ApplicationUpdate(
    const std::string& id,
    std::function<void(ErrorOr<bool> reply)> result) {
  auto shim = std::make_shared<FlatpakShim>(this, registrar_->messenger(),
                                            strand_.get());

  struct PromiseGuard {
    std::function<void(ErrorOr<bool>)> callback;
    std::once_flag flag;

    void call(ErrorOr<bool> error) {
      std::call_once(flag, [this, error = std::move(error)]() mutable {
        try {
          callback(std::move(error));
        } catch (const std::exception& e) {
          spdlog::error("Exception in ApplicationUpdate callback: {}",
                        e.what());
        } catch (...) {
          spdlog::error("Unknown exception in ApplicationUpdate callback");
        }
      });
    }
  };

  auto guard = std::make_shared<PromiseGuard>();
  guard->callback = std::move(result);

  asio::dispatch(*strand_, [this, id, guard, shim]() mutable {
    shim->SetupTransactionEventChannel(registrar_->messenger());

    shim->ApplicationUpdate(id, [guard](const ErrorOr<bool>& update_result) {
      if (update_result.has_error()) {
        FlutterError error("UPDATE_FAILED", update_result.error().message(),
                           flutter::EncodableValue());
        guard->call(ErrorOr<bool>(false));
      } else if (update_result.value()) {
        guard->call(ErrorOr<bool>(true));
      } else {
        FlutterError error("UPDATE_FAILED",
                           "Updating returned false without error",
                           flutter::EncodableValue());
        guard->call(ErrorOr<bool>(false));
      }
    });
  });
}

ErrorOr<bool> FlatpakPlugin::ApplicationStart(const std::string& id) {
  auto shim = std::make_shared<FlatpakShim>(this, registrar_->messenger(),
                                            strand_.get());

  std::promise<ErrorOr<bool>> promise;
  auto future = promise.get_future();

  asio::dispatch(*strand_, [this, id, &promise, shim]() {
    shim->SetupTransactionEventChannel(registrar_->messenger());
    auto result = shim->ApplicationStart(id, *strand_, portal_manager_);
    promise.set_value(std::move(result));
  });

  return future.get();
}

ErrorOr<bool> FlatpakPlugin::ApplicationStop(const std::string& id) {
  return FlatpakShim::ApplicationStop(id);
}

}  // namespace flatpak_plugin