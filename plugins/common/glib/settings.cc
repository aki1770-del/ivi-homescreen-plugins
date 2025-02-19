#include "settings.h"

#include <gio/gio.h>
#include <string>

#include <plugins/common/common.h>

namespace plugin_common_glib {

std::string ReadGSettingsKey(const std::string& schema,
                             const std::string& key) {
  GError* error = nullptr;
  GSettings* settings = g_settings_new(schema.c_str());
  if (!settings) {
    spdlog::error("Failed to create GSettings object for schema: {}", schema);
    return "";
  }

  gchar* value = g_settings_get_string(settings, key.c_str());
  if (!value) {
    spdlog::error("Failed to read key: {} from schema: {}", key, schema);
    g_object_unref(settings);
    return "";
  }

  std::string result(value);
  g_free(value);
  g_object_unref(settings);
  return result;
}

bool SetGSettingsKey(const std::string& schema,
                     const std::string& key,
                     const std::string& value) {
  if (schema.empty() || key.empty() || value.empty()) {
    spdlog::error("[SetGSettingsKey] Invalid parameters");
    return false;
  }

  GSettings* settings = g_settings_new(schema.c_str());
  if (!settings) {
    spdlog::error("Failed to create GSettings object for schema: {}", schema);
    return false;
  }

  const gboolean success =
      g_settings_set_string(settings, key.c_str(), value.c_str());
  if (!success) {
    spdlog::error("Failed to set key: {} to value: {} in schema: {}", key,
                  value, schema);
    g_object_unref(settings);
    return false;
  }

  g_object_unref(settings);
  return true;
}

}  // namespace plugin_common_glib