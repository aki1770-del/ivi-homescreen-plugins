/*
 * Copyright 2020-2025 Toyota Connected North America
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

#ifndef FLUTTER_PLUGIN_XDG_ICONS_PLUGIN_H
#define FLUTTER_PLUGIN_XDG_ICONS_PLUGIN_H

#include <flutter/plugin_registrar.h>

#include <filesystem>

namespace fs = std::filesystem;

#include "messages.h"

namespace plugin_xdg_icons {

class XdgIconsPlugin final : public flutter::Plugin, public XdgIconsApi {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar);

  XdgIconsPlugin();

  ~XdgIconsPlugin() override;

  ErrorOr<flutter::EncodableValue> LookupIcon(
      const flutter::EncodableMap& map) override;

  // Disallow copy and assign.
  XdgIconsPlugin(const XdgIconsPlugin&) = delete;
  XdgIconsPlugin& operator=(const XdgIconsPlugin&) = delete;

 private:
  std::optional<std::string> FindIconHelper(const std::string& icon,
                                            int size,
                                            int scale,
                                            const std::string& theme);

  static std::optional<std::string> LookupIcon(const std::string& iconname,
                                               int size,
                                               int scale,
                                               const std::string& theme);

  static std::optional<std::string> LookupFallbackIcon(
      const std::string& icon_name);

  std::optional<std::string> FindIcon(const std::string& icon,
                                      int size,
                                      int scale,
                                      const std::string& theme);

  static bool DirectoryMatchesSize(int icon_size, int icon_scale);

  static int DirectorySizeDistance(int icon_size, int icon_scale);
};

}  // namespace plugin_xdg_icons

#endif  // FLUTTER_PLUGIN_XDG_ICONS_PLUGIN_H
