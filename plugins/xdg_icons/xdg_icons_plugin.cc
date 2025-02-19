#include "xdg_icons_plugin.h"

#include <flutter/plugin_registrar.h>

#include <fstream>
#include <memory>

#include "common/common.h"
#include "common/glib/settings.h"

#include "messages.h"

namespace plugin_xdg_icons {

// static
void XdgIconsPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrar* registrar) {
  auto plugin = std::make_unique<XdgIconsPlugin>();

  SetUp(registrar->messenger(), plugin.get());

  registrar->AddPlugin(std::move(plugin));
}

XdgIconsPlugin::XdgIconsPlugin() = default;

XdgIconsPlugin::~XdgIconsPlugin() = default;

std::vector<uint8_t> ReadFileIntoVector(const std::string& filename) {
  std::ifstream file(filename, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + filename);
  }

  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

  std::vector<uint8_t> list;
  for (const auto& byte : bytes) {
    list.emplace_back(static_cast<std::uint8_t>(byte));
  }
  return list;
}

ErrorOr<flutter::EncodableMap> XdgIconsPlugin::LookupIcon(
    const flutter::EncodableMap& map) {
  if (map.empty()) {
    return FlutterError("argument_error", "no arguments provided");
  }

  std::string icon_name;
  std::string size;
  std::string scale;
  std::string theme;

  for (const auto& [key, value] : map) {
    std::string k, v;

    if (std::holds_alternative<std::string>(key)) {
      k = std::get<std::string>(key);
    }
    if (std::holds_alternative<std::string>(value)) {
      v = std::get<std::string>(value);
    }
    if (k.empty() || v.empty()) {
      continue;
    }

    if (k == "name") {
      icon_name = v;
    } else if (k == "size") {
      size = v;
    } else if (k == "scale") {
      scale = v;
    } else if (k == "theme") {
      theme = v;
    }

    spdlog::debug("{}={}", k, v);
  }

  if (icon_name.empty() || size.empty() || scale.empty()) {
    spdlog::error("[XDG Icons] Missing key in map");
    return FlutterError("argument_error", "missing key in map");
  }

  // use current theme if not specified
  if (theme.empty()) {
    theme = plugin_common_glib::ReadGSettingsKey("org.gnome.desktop.interface",
                                                 "icon-theme");
  } else {
    if (!plugin_common_glib::SetGSettingsKey("org.gnome.desktop.interface",
                                             "icon-theme", theme)) {
      spdlog::error("[xdg_icons] Failed to set icon theme");
      return FlutterError("gsettings_error", "failed to set icon theme");
    }
  }

  int icon_size = 0;
  try {
    icon_size = std::stoi(size);
  } catch (const std::invalid_argument& e) {
    std::cerr << "[xdg_icons] Invalid argument: " << e.what() << std::endl;
    return FlutterError("argument_error", "invalid argument");
  } catch (const std::out_of_range& e) {
    std::cerr << "[xdg_icons] Out of range: " << e.what() << std::endl;
    return FlutterError("argument_error", "out of range");
  }

  int icon_scale = 0;
  try {
    icon_scale = std::stoi(scale);
  } catch (const std::invalid_argument& e) {
    std::cerr << "[xdg_icons] Invalid argument: " << e.what() << std::endl;
    return FlutterError("argument_error", "invalid argument");
  } catch (const std::out_of_range& e) {
    std::cerr << "[xdg_icons] Out of range: " << e.what() << std::endl;
    return FlutterError("argument_error", "out of range");
  }

  auto result = FindIconHelper(icon_name, icon_size, icon_scale, theme);
  if (!result.has_value()) {
    return FlutterError("icon_error", "icon not found");
  }
  spdlog::info("XDG Icon: {}", result.value());

  const auto data = ReadFileIntoVector(result.value());
  flutter::EncodableMap response = {
      {flutter::EncodableValue("baseScale"),
       flutter::EncodableValue(static_cast<double>(1))},
      {flutter::EncodableValue("baseSize"),
       flutter::EncodableValue(static_cast<double>(1))},
      {flutter::EncodableValue("fileName"),
       flutter::EncodableValue(result.value().c_str())},
      {flutter::EncodableValue("isSymbolic"),
       flutter::EncodableValue(static_cast<bool>(false))},
      {flutter::EncodableValue("data"),
       flutter::EncodableValue(
           std::string(*data.data(), data.size()).c_str())}};

  return response;
}

// NOLINTNEXTLINE
std::optional<std::string> XdgIconsPlugin::FindIconHelper(
    const std::string& icon,
    const int size,
    const int scale,
    const std::string& theme) {
  auto filename = LookupIcon(icon, size, scale, theme);
  if (filename) {
    return filename;
  }

  // Base case: if no parents, return nullopt
  std::vector<std::string> parents = {/* theme.parents */};
  if (parents.empty()) {
    return std::nullopt;
  }

  for (const auto& parent : parents) {
    filename = FindIconHelper(icon, size, scale, parent);
    if (filename) {
      return filename;
    }
  }
  return std::nullopt;
}

std::optional<std::string> XdgIconsPlugin::FindIcon(const std::string& icon,
                                                    const int size,
                                                    const int scale) {
  const std::string userSelectedTheme =
      "user_selected_theme";  // Replace with actual user selected theme
  auto filename = FindIconHelper(icon, size, scale, userSelectedTheme);
  if (filename) {
    return filename;
  }

  filename = FindIconHelper(icon, size, scale, "hicolor");
  if (filename) {
    return filename;
  }

  return LookupFallbackIcon(icon);
}

std::optional<std::string> XdgIconsPlugin::LookupIcon(
    const std::string& iconname,
    const int size,
    const int scale,
    const std::string& theme) {
  std::vector<std::string> subdirs = {/* theme subdir list */};
  std::vector<std::string> directories = {/* basename list */};
  std::vector<std::string> extensions = {"png", "svg", "xpm"};

  for (const auto& subdir : subdirs) {
    for (const auto& directory : directories) {
      for (const auto& extension : extensions) {
        if (DirectoryMatchesSize(size, scale)) {
          fs::path filename = fs::path(directory) / theme / subdir /
                              (iconname + "." + extension);
          if (exists(filename)) {
            return filename.string();
          }
        }
      }
    }
  }

  int minimal_size = std::numeric_limits<int>::max();
  std::optional<std::string> closest_filename;

  for (const auto& subdir : subdirs) {
    for (const auto& directory : directories) {
      for (const auto& extension : extensions) {
        fs::path filename =
            fs::path(directory) / theme / subdir / (iconname + "." + extension);
        if (exists(filename)) {
          if (const int distance = DirectorySizeDistance(size, scale);
              distance < minimal_size) {
            closest_filename = filename.string();
            minimal_size = distance;
          }
        }
      }
    }
  }

  return closest_filename;
}

std::optional<std::string> XdgIconsPlugin::LookupFallbackIcon(
    const std::string& icon_name) {
  std::vector<std::string> directories = {/* basename list */};
  std::vector<std::string> extensions = {"png", "svg", "xpm"};

  for (const auto& directory : directories) {
    for (const auto& extension : extensions) {
      std::string filename = icon_name;
      filename.append(".");
      filename.append(extension);
      if (fs::path filepath = fs::path(directory) / filename;
          exists(filepath)) {
        return filepath.string();
      }
    }
  }
  return std::nullopt;
}

bool XdgIconsPlugin::DirectoryMatchesSize(const int icon_size,
                                          const int icon_scale) {
  // Read Type and size data from subdir
  // Assuming we have functions to get these values
  const std::string type = "Fixed";  // Replace with actual type
  constexpr int size = 0;            // Replace with actual size

  if (constexpr int scale = 1; scale != icon_scale) {
    return false;
  }
  if (type == "Fixed") {
    return size == icon_size;
  }
  if (type == "Scaled") {
    constexpr int maxSize = 0;
    constexpr int minSize = 0;
    return minSize <= icon_size && icon_size <= maxSize;
  }
  if (type == "Threshold") {
    constexpr int threshold = 0;
    return size - threshold <= icon_size && icon_size <= size + threshold;
  }
  return false;
}

int XdgIconsPlugin::DirectorySizeDistance(const int icon_size,
                                          const int icon_scale) {
  // Read Type and size data from subdir
  // Assuming we have functions to get these values
  const std::string type = "Fixed";  // Replace with actual type
  constexpr int size = 0;            // Replace with actual size
  constexpr int scale = 1;           // Replace with actual scale
  constexpr int minSize = 0;         // Replace with actual min size
  constexpr int maxSize = 0;         // Replace with actual max size

  if (type == "Fixed") {
    return std::abs(size * scale - icon_size * icon_scale);
  }
  if (type == "Scaled") {
    if (icon_size * icon_scale < minSize * scale) {
      return minSize * scale - icon_size * icon_scale;
    }
    if (icon_size * icon_scale > maxSize * scale) {
      return icon_size * icon_scale - maxSize * scale;
    }
    return 0;
  }
  if (type == "Threshold") {
    constexpr int threshold = 0;
    if (icon_size * icon_scale < (size - threshold) * scale) {
      return minSize * scale - icon_size * icon_scale;
    }
    if (icon_size * icon_scale > (size + threshold) * scale) {
      return icon_size * icon_scale - maxSize * scale;
    }
    return 0;
  }
  return std::numeric_limits<int>::max();
}

}  // namespace plugin_xdg_icons