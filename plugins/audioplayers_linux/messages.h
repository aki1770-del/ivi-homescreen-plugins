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

#ifndef PLUGIN_AUDIOPLAYERS_LINUX_MESSAGES_H_
#define PLUGIN_AUDIOPLAYERS_LINUX_MESSAGES_H_

#include <flutter/binary_messenger.h>

namespace audioplayers_linux_plugin {

void SetupMethodChannel(flutter::BinaryMessenger* messenger);
void SetupGlobalMethodChannel(flutter::BinaryMessenger* messenger);
void SetupGlobalEventChannel(flutter::BinaryMessenger* messenger);

}  // namespace audioplayers_linux_plugin

#endif  // PLUGIN_AUDIOPLAYERS_LINUX_MESSAGES_H_
