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

#ifndef FLUTTER_PLUGIN_AUDIO_PLAYERS_PLUGIN_H_
#define FLUTTER_PLUGIN_AUDIO_PLAYERS_PLUGIN_H_

#include <flutter/binary_messenger.h>
#include <flutter/plugin_registrar.h>

#include "audio_player.h"

namespace audioplayers_linux_plugin {

class AudioplayersLinuxPlugin final : public Plugin {
 public:
  static void RegisterWithRegistrar(PluginRegistrar* registrar);

  explicit AudioplayersLinuxPlugin(BinaryMessenger* messenger);

  ~AudioplayersLinuxPlugin() override;

  static AudioPlayer* GetPlayer(const std::string& playerId);
  static AudioPlayer* CreatePlayer(const std::string& playerId,
                                   flutter::BinaryMessenger* messenger);
  static void DisposePlayer(const std::string& playerId);

  AudioplayersLinuxPlugin(const AudioplayersLinuxPlugin&) = delete;
  AudioplayersLinuxPlugin& operator=(const AudioplayersLinuxPlugin&) = delete;

 private:
  flutter::BinaryMessenger* messenger_;
};

}  // namespace audioplayers_linux_plugin

#endif  // FLUTTER_PLUGIN_AUDIO_PLAYERS_PLUGIN_H_
