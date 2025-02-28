#include "xdg_icons_plugin.h"

#include <flutter/plugin_registrar.h>

#include <fstream>

#include "common/common.h"
#include "common/glib/settings.h"

#include "messages.h"
#include "plugins/common/inipp.h"

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
    spdlog::error("[xdg_icons] Failed to open file: {}", filename);
    return {};
  }

  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());

  std::vector<uint8_t> list;
  list.reserve(bytes.size());
  for (const auto& byte : bytes) {
    list.emplace_back(static_cast<std::uint8_t>(byte));
  }
  return list;
}

std::vector<fs::path> GetIconThemePaths(const std::string& theme) {
  std::vector<fs::path> icon_paths;

  // Add $HOME/.icons to the vector if present
  if (const char* home = std::getenv("HOME")) {
    fs::path home_path(home);
    home_path /= ".icons";
    home_path /= theme;
    if (exists(home_path)) {
      icon_paths.emplace_back(home_path);
    }
  }

  // Add each path in $XDG_DATA_DIRS if present
  if (const char* xdg_data_dirs = std::getenv("XDG_DATA_DIRS")) {
    std::string xdg_data_dirs_str(xdg_data_dirs);
    size_t start = 0;
    size_t end = xdg_data_dirs_str.find(':');
    while (end != std::string::npos) {
      std::string dir = xdg_data_dirs_str.substr(start, end - start);
      fs::path path(dir);
      path /= "icons";
      path /= theme;
      if (exists(path)) {
        icon_paths.emplace_back(path);
      }
      start = end + 1;
      end = xdg_data_dirs_str.find(':', start);
    }
    std::string dir = xdg_data_dirs_str.substr(start);
    fs::path path(dir);
    path /= "icons";
    path /= theme;
    if (exists(path)) {
      icon_paths.emplace_back(path);
    }
  }

  // Add /usr/share/pixmaps/icons if present
  fs::path path("/usr/share/pixmaps");
  path /= "icons";
  path /= theme;
  if (exists(path)) {
    icon_paths.emplace_back(path);
  }

  for (const auto& it : icon_paths) {
    spdlog::debug("icon path: {}", it.string());
  }

  return icon_paths;
}

std::vector<std::string> ParseIniString(const std::string& input,
                                        char delimiter) {
  std::vector<std::string> result;
  std::stringstream ss(input);
  std::string item;

  while (std::getline(ss, item, delimiter)) {
    if (!item.empty()) {
      result.push_back(item);
    }
  }

  return result;
}

// NOLINTNEXTLINE
void GetThemePaths(const std::string& theme, std::vector<std::string>& paths) {
  std::vector<fs::path> theme_paths = GetIconThemePaths(theme);
  for (const auto& theme_path : theme_paths) {
    fs::path theme_filepath = theme_path / "index.theme";
    if (!exists(theme_filepath)) {
      continue;
    }

    // Parse the theme file
    inipp::Ini<char> ini;
    std::ifstream f(theme_filepath.string().c_str(), std::ios::binary);
    if (!f) {
      spdlog::error("[xdg_icons] Failed to open theme file: {}",
                    theme_filepath.string());
      return;
    }
    ini.parse(f);
    f.close();

    std::string inherits = ini.sections["Icon Theme"].at("Inherits");
    auto inherit_themes = ParseIniString(inherits, ',');
    for (const auto& it : inherit_themes) {
      spdlog::debug("Inherits themes: {}", it);
    }

    std::string directories = ini.sections["Icon Theme"].at("Directories");
    auto theme_directories = ParseIniString(directories, ',');

    for (const auto& it : theme_directories) {
      fs::path dir = theme_path;
      dir /= it;
      if (is_directory(dir) && exists(dir)) {
        paths.emplace_back(dir.string());
      }
    }
  }
}

ErrorOr<flutter::EncodableValue> XdgIconsPlugin::LookupIcon(
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

  if (icon_name.empty()) {
    spdlog::error("[XDG Icons] Missing icon name in args");
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
  if (!size.empty()) {
    try {
      icon_size = std::stoi(size);
    } catch (const std::invalid_argument& e) {
      std::cerr << "[xdg_icons] Invalid argument: " << e.what() << std::endl;
      return FlutterError("argument_error", "invalid argument");
    } catch (const std::out_of_range& e) {
      std::cerr << "[xdg_icons] Out of range: " << e.what() << std::endl;
      return FlutterError("argument_error", "out of range");
    }
  }

  int icon_scale = 1;
  if (!scale.empty()) {
    try {
      icon_scale = std::stoi(scale);
    } catch (const std::invalid_argument& e) {
      std::cerr << "[xdg_icons] Invalid argument: " << e.what() << std::endl;
      return FlutterError("argument_error", "invalid argument");
    } catch (const std::out_of_range& e) {
      std::cerr << "[xdg_icons] Out of range: " << e.what() << std::endl;
      return FlutterError("argument_error", "out of range");
    }
  }

  fs::path icon_path = icon_name + ".png";

  std::vector<std::string> paths;
  GetThemePaths(theme, paths);
  for (const auto& p : paths) {
    fs::path p1(p);
    p1 /= icon_path;
    if (exists(p1)) {
      spdlog::debug("icon found: {}", p1.string());
    }
  }

  auto result = FindIconHelper(icon_name, icon_size, icon_scale, theme);
  if (!result.has_value()) {
    return FlutterError("icon_error", "icon not found");
  }
  spdlog::info("XDG Icon: {}", result.value());

  const auto data = ReadFileIntoVector(result.value());
  flutter::EncodableMap response = {
      {flutter::EncodableValue("baseScale"), flutter::EncodableValue(1.0)},
      {flutter::EncodableValue("baseSize"), flutter::EncodableValue(1.0)},
      {flutter::EncodableValue("fileName"),
       flutter::EncodableValue(result.value().c_str())},
      {flutter::EncodableValue("isSymbolic"), flutter::EncodableValue(false)},
      {flutter::EncodableValue("data"),
       flutter::EncodableValue(std::string(data.begin(), data.end()))}};

  return flutter::EncodableValue(response);
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
                                                    const int scale,
                                                    const std::string& theme) {
  auto filename = FindIconHelper(icon, size, scale, theme);
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
  return {};
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