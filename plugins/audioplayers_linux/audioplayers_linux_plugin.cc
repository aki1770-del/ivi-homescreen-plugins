/*
 * Copyright 2020 Toyota Connected North America
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

#include "audioplayers_linux_plugin.h"

#include <map>
#include <memory>
#include <string>

#include <flutter/plugin_registrar.h>

#include "messages.h"
#include "plugins/common/glib/main_loop.h"

namespace audioplayers_linux_plugin {

static std::map<std::string, std::unique_ptr<AudioPlayer>> audioPlayers_;

// static
void AudioplayersLinuxPlugin::RegisterWithRegistrar(
    PluginRegistrar* registrar) {
  auto plugin =
      std::make_unique<AudioplayersLinuxPlugin>(registrar->messenger());

  SetupMethodChannel(registrar->messenger());
  SetupGlobalMethodChannel(registrar->messenger());
  SetupGlobalEventChannel(registrar->messenger());

  registrar->AddPlugin(std::move(plugin));
}

AudioplayersLinuxPlugin::AudioplayersLinuxPlugin(BinaryMessenger* messenger)
    : messenger_(messenger) {
  audioPlayers_.clear();

  // GStreamer lib only needs to be initialized once.  Calling it multiple times
  // is fine.
  gst_init(nullptr, nullptr);

  // start the main loop if not already running
  plugin_common_glib::MainLoop::GetInstance();
}

AudioplayersLinuxPlugin::~AudioplayersLinuxPlugin() = default;

AudioPlayer* AudioplayersLinuxPlugin::GetPlayer(const std::string& playerId) {
  const auto searchPlayer = audioPlayers_.find(playerId);
  if (searchPlayer == audioPlayers_.end()) {
    return nullptr;
  }
  return searchPlayer->second.get();
}

void AudioplayersLinuxPlugin::DisposePlayer(const std::string& playerId) {
  const auto it = audioPlayers_.find(playerId);
  if (it == audioPlayers_.end()) {
    return;
  }
  it->second->Dispose();
  audioPlayers_.erase(it);
}

AudioPlayer* AudioplayersLinuxPlugin::CreatePlayer(
    const std::string& playerId,
    flutter::BinaryMessenger* messenger) {
  if (const auto existing = audioPlayers_.find(playerId);
      existing != audioPlayers_.end()) {
    return existing->second.get();
  }
  std::string event_channel = "xyz.luan/audioplayers/events/" + playerId;
  auto player =
      std::make_unique<AudioPlayer>(std::move(event_channel), messenger);
  auto* raw = player.get();
  audioPlayers_.insert(std::make_pair(playerId, std::move(player)));
  return raw;
}

}  // namespace audioplayers_linux_plugin
