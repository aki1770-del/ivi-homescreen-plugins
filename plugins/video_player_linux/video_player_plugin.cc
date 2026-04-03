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

#include "video_player_plugin.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include <gst/pbutils/pbutils.h>

#include "messages.g.h"
#include "plugins/common/glib/main_loop.h"
#include "video_player.h"

namespace video_player_linux {

// static
void VideoPlayerPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarDesktop* registrar) {
  auto plugin = std::make_unique<VideoPlayerPlugin>(registrar);
  SetUp(registrar->messenger(), plugin.get());
  registrar->AddPlugin(std::move(plugin));
}

VideoPlayerPlugin::~VideoPlayerPlugin() = default;

VideoPlayerPlugin::VideoPlayerPlugin(flutter::PluginRegistrarDesktop* registrar)
    : registrar_(registrar) {
  // GStreamer lib only needs to be initialized once.  Calling it multiple times
  // is fine.
  gst_init(nullptr, nullptr);

  // start the main loop if not already running
  plugin_common_glib::MainLoop::GetInstance();
}

std::optional<FlutterError> VideoPlayerPlugin::Initialize() {
  for (auto& [fst, snd] : videoPlayers) {
    snd->Dispose();
  }
  videoPlayers.clear();
  return std::nullopt;
}

static bool is_allowed_uri_scheme(const std::string& uri) {
  static constexpr std::array<const char*, 4> kAllowedSchemes = {
      "file://", "http://", "https://", "rtsp://"};
  return std::any_of(kAllowedSchemes.begin(), kAllowedSchemes.end(),
                     [&uri](const char* scheme) {
                       return uri.compare(0, strlen(scheme), scheme) == 0;
                     });
}

static bool has_header_injection(const std::string& value) {
  return value.find('\r') != std::string::npos ||
         value.find('\n') != std::string::npos ||
         value.find('\0') != std::string::npos;
}

ErrorOr<int64_t> VideoPlayerPlugin::Create(
    const std::string* asset,
    const std::string* uri,
    const flutter::EncodableMap& http_headers) {
  std::string asset_to_load;
  std::map<std::string, std::string> http_headers_;

  std::unique_ptr<VideoPlayer> player;
  if (asset && !asset->empty()) {
    asset_to_load = "file://";
    std::filesystem::path path;
    if (asset->c_str()[0] == '/') {
      path /= asset->c_str();
    } else {
      path = registrar_->flutter_asset_folder();
      SPDLOG_DEBUG("path: [{}]", registrar_->flutter_asset_folder());
      path /= asset->c_str();
    }
    if (!exists(path)) {
      spdlog::error("[VideoPlayer] Asset Path does not exist. {}",
                    path.c_str());
      return FlutterError("asset_load_failed", "Asset Path does not exist.");
    }
    asset_to_load += path.c_str();
  } else if (uri && !uri->empty()) {
    if (!is_allowed_uri_scheme(*uri)) {
      spdlog::error("[VideoPlayer] Unsupported URI scheme: {}", *uri);
      return FlutterError("uri_load_failed",
                          "URI scheme not allowed. "
                          "Supported: file, http, https, rtsp");
    }
    asset_to_load = *uri;

    for (const auto& [key, value] : http_headers) {
      if (std::holds_alternative<std::string>(key) &&
          std::holds_alternative<std::string>(value)) {
        const auto& k = std::get<std::string>(key);
        const auto& v = std::get<std::string>(value);
        if (has_header_injection(k) || has_header_injection(v)) {
          spdlog::error(
              "[VideoPlayer] Rejected HTTP header with control characters");
          return FlutterError("invalid_headers",
                              "HTTP header contains invalid characters");
        }
        http_headers_[k] = v;
      }
    }
  } else {
    return FlutterError("not_implemented", "Set either an asset or a uri");
  }

  SPDLOG_DEBUG("[VideoPlayer] asset: {}", asset_to_load);

  try {
    int width = 0, height = 0;
    gint64 duration = 0;
    if (!discover_video_info(asset_to_load.c_str(), width, height, duration)) {
      return FlutterError("video_info_failed",
                          "Failed to discover video information");
    }

    player = std::make_unique<VideoPlayer>(registrar_, asset_to_load.c_str(),
                                           std::move(http_headers_), width,
                                           height, duration);

  } catch (std::exception& e) {
    return FlutterError("uri_load_failed", e.what());
  }

  player->Init(registrar_->messenger());

  auto texture_id = player->GetTextureId();

  videoPlayers.insert(std::make_pair(texture_id, std::move(player)));

  return texture_id;
}

std::optional<FlutterError> VideoPlayerPlugin::Dispose(
    const int64_t texture_id) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->Dispose();
    videoPlayers.erase(texture_id);
  }

  return {};
}

std::optional<FlutterError> VideoPlayerPlugin::SetLooping(
    const int64_t texture_id,
    const bool is_looping) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->SetLooping(is_looping);
  }

  return {};
}

std::optional<FlutterError> VideoPlayerPlugin::SetVolume(
    const int64_t texture_id,
    const double volume) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->SetVolume(volume);
  }

  return {};
}

std::optional<FlutterError> VideoPlayerPlugin::SetPlaybackSpeed(
    const int64_t texture_id,
    const double speed) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->SetPlaybackSpeed(speed);
  }

  return {};
}

std::optional<FlutterError> VideoPlayerPlugin::Play(const int64_t texture_id) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->Play();
  }

  return {};
}

ErrorOr<int64_t> VideoPlayerPlugin::GetPosition(const int64_t texture_id) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  int64_t position = 0;
  if (searchPlayer != videoPlayers.end()) {
    if (const std::unique_ptr<VideoPlayer>& player = searchPlayer->second;
        player->IsValid()) {
      position = player->GetPosition();
      //      player->SendBufferingUpdate();
    }
  }
  return position;
}

std::optional<FlutterError> VideoPlayerPlugin::SeekTo(const int64_t texture_id,
                                                      const int64_t position) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->SeekTo(position);
  }

  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::Pause(const int64_t texture_id) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->Pause();
  }

  return std::nullopt;
}

bool VideoPlayerPlugin::discover_video_info(const char* url,
                                            int& width,
                                            int& height,
                                            gint64& duration) {
  GError* err = nullptr;
  GstDiscoverer* discoverer = gst_discoverer_new(10 * GST_SECOND, &err);
  if (!discoverer) {
    spdlog::error("[VideoPlayer] Failed to create discoverer: {}",
                  err ? err->message : "unknown");
    g_clear_error(&err);
    return false;
  }

  GstDiscovererInfo* info = gst_discoverer_discover_uri(discoverer, url, &err);
  if (!info || gst_discoverer_info_get_result(info) != GST_DISCOVERER_OK) {
    spdlog::error("[VideoPlayer] Discovery failed for {}: {}", url,
                  err ? err->message : "unknown");
    g_clear_error(&err);
    if (info)
      gst_discoverer_info_unref(info);
    g_object_unref(discoverer);
    return false;
  }

  duration = static_cast<gint64>(gst_discoverer_info_get_duration(info));

  GList* video_streams = gst_discoverer_info_get_video_streams(info);
  if (video_streams) {
    auto* vinfo = static_cast<GstDiscovererVideoInfo*>(video_streams->data);
    width = static_cast<int>(gst_discoverer_video_info_get_width(vinfo));
    height = static_cast<int>(gst_discoverer_video_info_get_height(vinfo));
    gst_discoverer_stream_info_list_free(video_streams);
  } else {
    spdlog::error("[VideoPlayer] No video stream found in {}", url);
    gst_discoverer_info_unref(info);
    g_object_unref(discoverer);
    return false;
  }

  SPDLOG_DEBUG("[VideoPlayer] Discovered: {}x{}, duration={}ns", width, height,
               duration);

  gst_discoverer_info_unref(info);
  g_object_unref(discoverer);
  return true;
}

}  // namespace video_player_linux