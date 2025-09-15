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
#include <asio/post.hpp>

#include "messages.g.h"
#include "plugins/common/common.h"

namespace flatpak_plugin {

// static
void FlatpakPlugin::RegisterWithRegistrar(flutter::PluginRegistrar* registrar) {
  auto plugin = std::make_unique<FlatpakPlugin>();

  SetUp(registrar->messenger(), plugin.get());

  registrar->AddPlugin(std::move(plugin));
}

FlatpakPlugin::FlatpakPlugin()
    : io_context_(std::make_unique<asio::io_context>(ASIO_CONCURRENCY_HINT_1)),
      work_(io_context_->get_executor()),
      strand_(std::make_unique<asio::io_context::strand>(*io_context_)) {
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
  manager_ = CacheManager::Builder()
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
  auto result = manager_->GetUserInstallation(false);
  return ErrorOr<Installation>(std::move(result.value()));
}

ErrorOr<flutter::EncodableList> FlatpakPlugin::GetSystemInstallations() {
  auto result = manager_->GetSystemInstallations(false);
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
  auto result = manager_->GetApplicationsInstalled(false);
  if (!result.has_value()) {
    return ErrorOr<flutter::EncodableList>(flutter::EncodableList());
  }
  return ErrorOr<flutter::EncodableList>(std::move(result.value()));
}

ErrorOr<flutter::EncodableList> FlatpakPlugin::GetApplicationsRemote(
    const std::string& id) {
  auto result = manager_->GetApplicationsRemote(id, false);
  if (!result.has_value()) {
    return ErrorOr<flutter::EncodableList>(flutter::EncodableList());
  }
  return ErrorOr<flutter::EncodableList>(std::move(result.value()));
}

ErrorOr<bool> FlatpakPlugin::ApplicationInstall(const std::string& id) {
  return FlatpakShim::ApplicationInstall(id);
}

ErrorOr<bool> FlatpakPlugin::ApplicationUninstall(const std::string& id) {
  return FlatpakShim::ApplicationUninstall(id);
}

ErrorOr<bool> FlatpakPlugin::ApplicationStart(
    const std::string& id,
    const flutter::EncodableMap* configuration) {
  return FlatpakShim::ApplicationStart(id, configuration);
}

ErrorOr<bool> FlatpakPlugin::ApplicationStop(const std::string& id) {
  return FlatpakShim::ApplicationStop(id);
}

}  // namespace flatpak_plugin