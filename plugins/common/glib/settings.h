/*
 * Copyright 2023-2025 Toyota Connected North America
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

#ifndef PLUGINS_COMMON_GLIB_SETTINGS_H_
#define PLUGINS_COMMON_GLIB_SETTINGS_H_

#include <string>

namespace plugin_common_glib {

std::string ReadGSettingsKey(const std::string& schema, const std::string& key);

bool SetGSettingsKey(const std::string& schema,
                     const std::string& key,
                     const std::string& value);

}  // namespace plugin_common_glib

#endif  // PLUGINS_COMMON_GLIB_SETTINGS_H_