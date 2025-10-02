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

#include "flatpak_shim.h"

#include <filesystem>

#include <libxml/tree.h>
#include <libxml/xmlstring.h>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <zlib.h>

#include <fstream>

#include "plugins/common/common.h"

#include "appstream_catalog.h"
#include "asio/post.hpp"
#include "component.h"
#include "cxxopts/include/cxxopts.hpp"
#include "messages.g.h"
#include "portals.h"
#include "screenshot.h"

namespace flatpak_plugin {

std::unique_ptr<Portals> portal_;
std::unordered_map<std::string, FlatpakShim::Instance> FlatpakShim::instances;

std::optional<std::string> FlatpakShim::getOptionalAttribute(
    const xmlNode* node,
    const char* attrName) {
  if (xmlChar* xmlValue =
          xmlGetProp(node, reinterpret_cast<const xmlChar*>(attrName))) {
    std::string value(reinterpret_cast<const char*>(xmlValue));
    xmlFree(xmlValue);
    return value;
  }
  return std::nullopt;
}

std::string FlatpakShim::getAttribute(const xmlNode* node,
                                      const char* attrName) {
  if (xmlChar* xmlValue =
          xmlGetProp(node, reinterpret_cast<const xmlChar*>(attrName))) {
    std::string value(reinterpret_cast<const char*>(xmlValue));
    xmlFree(xmlValue);
    return value;
  }
  return "";
}

void FlatpakShim::PrintComponent(const Component& component) {
  spdlog::info("[FlatpakPlugin] Component [{}]", component.getId());
  spdlog::info("[FlatpakPlugin] \tName: {}", component.getName());
  spdlog::info("[FlatpakPlugin] \tPackage Name: {}", component.getPkgName());
  spdlog::info("[FlatpakPlugin] \tSummary: {}", component.getSummary());

  if (component.getReleases().has_value()) {
    spdlog::info("[FlatpakPlugin] \tReleases: ");
    for (const auto& release : component.getReleases().value()) {
      spdlog::info("[FlatpakPlugin] \t\tVersion: {}", release.getVersion());
      spdlog::info("[FlatpakPlugin] \t\tTimestamp: {}", release.getTimestamp());
      if (release.getDescription().has_value()) {
        spdlog::info("[FlatpakPlugin] \t\tDescription: {}",
                     release.getDescription().value());
      }
      if (release.getSize().has_value()) {
        spdlog::info("[FlatpakPlugin] \t\tSize: {}", release.getSize().value());
      }
    }
  }

  // Checking and printing optional fields
  if (component.getVersion().has_value()) {
    spdlog::info("[FlatpakPlugin] \tVersion: {}",
                 component.getVersion().value());
  }
  if (component.getOrigin().has_value()) {
    spdlog::info("[FlatpakPlugin] \tOrigin: {}", component.getOrigin().value());
  }
  if (component.getMediaBaseurl().has_value()) {
    spdlog::info("[FlatpakPlugin] \tMedia Base URL: {}",
                 component.getMediaBaseurl().value());
  }
  if (component.getArchitecture().has_value()) {
    spdlog::info("[FlatpakPlugin] \tArchitecture: {}",
                 component.getArchitecture().value());
  }
  if (component.getProjectLicense().has_value()) {
    spdlog::info("[FlatpakPlugin] \tProject License: {}",
                 component.getProjectLicense().value());
  }
  if (component.getDescription().has_value()) {
    spdlog::info("[FlatpakPlugin] \tDescription: {}",
                 component.getDescription().value());
  }
  if (component.getUrl().has_value()) {
    spdlog::info("[FlatpakPlugin] \tURL: {}", component.getUrl().value());
  }
  if (component.getProjectGroup().has_value()) {
    spdlog::info("[FlatpakPlugin] \tProject Group: {}",
                 component.getProjectGroup().value());
  }
  if (component.getIcons().has_value()) {
    spdlog::info("[FlatpakPlugin] \tIcons:");
    for (const auto& icon : component.getIcons().value()) {
      icon.printIconDetails();
    }
  }
  if (component.getCategories().has_value()) {
    spdlog::info("[FlatpakPlugin] \tCategories:");
    for (const auto& category : component.getCategories().value()) {
      spdlog::info("[FlatpakPlugin] \t\t{}", category);
    }
  }
  if (component.getScreenshots().has_value()) {
    for (const auto& screenshot : component.getScreenshots().value()) {
      screenshot.printScreenshotDetails();
    }
  }
  if (component.getKeywords().has_value()) {
    spdlog::info("[FlatpakPlugin] \tKeywords:");
    for (const auto& keyword : component.getKeywords().value()) {
      spdlog::info("[FlatpakPlugin] \t\t{}", keyword);
    }
  }
  // Additional optional fields
  if (component.getSourcePkgname().has_value()) {
    spdlog::info("[FlatpakPlugin] \tSource Pkgname: {}",
                 component.getSourcePkgname().value());
  }
  if (component.getBundle().has_value()) {
    spdlog::info("[FlatpakPlugin] \tBundle: {}", component.getBundle().value());
  }
  if (component.getContentRatingType().has_value()) {
    spdlog::info("[FlatpakPlugin] \tContent Rating Type: [{}]",
                 component.getContentRatingType().value());
  }
  if (component.getContentRating().has_value()) {
    if (!component.getContentRating().value().empty()) {
      spdlog::info("[FlatpakPlugin] \tContent Rating:");
      for (const auto& [key, value] : component.getContentRating().value()) {
        spdlog::info("[FlatpakPlugin] \t\t{} = {}", key,
                     Component::RatingValueToString(value));
      }
    }
  }
  if (component.getAgreement().has_value()) {
    spdlog::info("[FlatpakPlugin] \tAgreement: {}",
                 component.getAgreement().value());
  }
}

GPtrArray* FlatpakShim::get_system_installations() {
  GError* error = nullptr;
  const auto sys_installs = flatpak_get_system_installations(nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error getting system installations: {}",
                  error->message);
    g_ptr_array_unref(sys_installs);
    g_clear_error(&error);
  }
  return sys_installs;
}

GPtrArray* FlatpakShim::get_remotes(FlatpakInstallation* installation) {
  GError* error = nullptr;
  const auto remotes =
      flatpak_installation_list_remotes(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error listing remotes: {}", error->message);
    g_clear_error(&error);
  }
  return remotes;
}

std::time_t FlatpakShim::get_appstream_timestamp(
    const std::filesystem::path& timestamp_filepath) {
  if (timestamp_filepath.empty() || !exists(timestamp_filepath)) {
    spdlog::debug(
        "[FlatpakPlugin] appstream_timestamp path is empty or does not exist: "
        "{}",
        timestamp_filepath.string());
    return std::time(nullptr);  // Return current time as fallback
  }

  try {
    const auto fileTime = std::filesystem::last_write_time(timestamp_filepath);
    const auto sctp =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            fileTime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sctp);
  } catch (const std::exception& e) {
    spdlog::warn("[FlatpakPlugin] Failed to get timestamp for {}: {}",
                 timestamp_filepath.string(), e.what());
    return std::time(nullptr);
  }
}

void FlatpakShim::format_time_iso8601(const time_t raw_time,
                                      char* buffer,
                                      const size_t buffer_size) {
  if (!buffer || buffer_size < 32) {
    spdlog::error("[FlatpakPlugin] Invalid buffer for time formatting");
    return;
  }

  tm tm_info{};
  if (localtime_r(&raw_time, &tm_info) == nullptr) {
    spdlog::error("[FlatpakPlugin] Failed to convert time");
    strncpy(buffer, "1970-01-01T00:00:00+00:00", buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return;
  }

  strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%S", &tm_info);
  long timezone_offset = tm_info.tm_gmtoff;
  const char sign = (timezone_offset >= 0) ? '+' : '-';
  timezone_offset = std::abs(timezone_offset);

  size_t len = strlen(buffer);
  snprintf(buffer + len, buffer_size - len, "%c%02ld:%02ld", sign,
           timezone_offset / 3600, (timezone_offset % 3600) / 60);
}

flutter::EncodableList FlatpakShim::installation_get_default_languages(
    FlatpakInstallation* installation) {
  flutter::EncodableList languages;
  GError* error = nullptr;
  const auto default_languages =
      flatpak_installation_get_default_languages(installation, &error);

  if (error) {
    spdlog::error("[FlatpakPlugin] Error getting default languages: {}",
                  error->message);
    g_error_free(error);
    return languages;
  }

  if (default_languages) {
    for (auto language = default_languages; *language; ++language) {
      languages.emplace_back(static_cast<const char*>(*language));
    }
    g_strfreev(default_languages);
  }
  return languages;
}

flutter::EncodableList FlatpakShim::installation_get_default_locales(
    FlatpakInstallation* installation) {
  flutter::EncodableList locales;
  GError* error = nullptr;

  const auto default_locales =
      flatpak_installation_get_default_locales(installation, &error);
  if (error) {
    spdlog::error(
        "[FlatpakPlugin] flatpak_installation_get_default_locales: {}",
        error->message);
    g_error_free(error);
    error = nullptr;
  }
  if (default_locales != nullptr) {
    for (auto locale = default_locales; *locale != nullptr; ++locale) {
      locales.emplace_back(static_cast<const char*>(*locale));
    }
    g_strfreev(default_locales);
  }
  return locales;
}

std::string FlatpakShim::FlatpakRemoteTypeToString(
    const FlatpakRemoteType type) {
  switch (type) {
    case FLATPAK_REMOTE_TYPE_STATIC:
      // Statically configured remote
      return "Static";
    case FLATPAK_REMOTE_TYPE_USB:
      // Dynamically detected local pathname remote
      return "USB";
    case FLATPAK_REMOTE_TYPE_LAN:
      // Dynamically detected network remote
      return "LAN";
  }
  return "Unknown";
}

Installation FlatpakShim::get_installation(FlatpakInstallation* installation) {
  flutter::EncodableList remote_list;

  if (const auto remotes = get_remotes(installation)) {
    for (size_t j = 0; j < remotes->len; j++) {
      const auto remote =
          static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, j));

      if (!remote) {
        spdlog::warn("[FlatpakPlugin] Null remote at index {}", j);
        continue;
      }

      try {
        const auto name = flatpak_remote_get_name(remote);
        const auto url = flatpak_remote_get_url(remote);

        // Validate required fields before creating a Remote object
        if (!name || !url) {
          spdlog::warn(
              "[FlatpakPlugin] Skipping remote with missing name or URL");
          continue;
        }

        const auto collection_id = flatpak_remote_get_collection_id(remote);
        const auto title = flatpak_remote_get_title(remote);
        const auto comment = flatpak_remote_get_comment(remote);
        const auto description = flatpak_remote_get_description(remote);
        const auto homepage = flatpak_remote_get_homepage(remote);
        const auto icon = flatpak_remote_get_icon(remote);
        const auto default_branch = flatpak_remote_get_default_branch(remote);
        const auto main_ref = flatpak_remote_get_main_ref(remote);
        const auto filter = flatpak_remote_get_filter(remote);
        const bool gpg_verify = flatpak_remote_get_gpg_verify(remote);
        const bool no_enumerate = flatpak_remote_get_noenumerate(remote);
        const bool no_deps = flatpak_remote_get_nodeps(remote);
        const bool disabled = flatpak_remote_get_disabled(remote);
        const int32_t prio = flatpak_remote_get_prio(remote);

        const auto default_arch = flatpak_get_default_arch();
        auto appstream_timestamp_path = g_file_get_path(
            flatpak_remote_get_appstream_timestamp(remote, default_arch));
        auto appstream_dir_path = g_file_get_path(
            flatpak_remote_get_appstream_dir(remote, default_arch));

        auto appstream_timestamp = get_appstream_timestamp(
            appstream_timestamp_path ? appstream_timestamp_path : "");
        char formatted_time[64] = {0};  // Initialize buffer
        format_time_iso8601(appstream_timestamp, formatted_time,
                            sizeof(formatted_time));

        remote_list.emplace_back(flutter::CustomEncodableValue(Remote(
            std::string(name), std::string(url),
            collection_id ? std::string(collection_id) : "",
            title ? std::string(title) : "",
            comment ? std::string(comment) : "",
            description ? std::string(description) : "",
            homepage ? std::string(homepage) : "",
            icon ? std::string(icon) : "",
            default_branch ? std::string(default_branch) : "",
            main_ref ? std::string(main_ref) : "",
            FlatpakRemoteTypeToString(flatpak_remote_get_remote_type(remote)),
            filter ? std::string(filter) : "", std::string(formatted_time),
            appstream_dir_path ? std::string(appstream_dir_path) : "",
            gpg_verify, no_enumerate, no_deps, disabled,
            static_cast<int64_t>(prio))));

        // Clean up allocated memory
        if (appstream_timestamp_path) {
          g_free(appstream_timestamp_path);
        }
        if (appstream_dir_path) {
          g_free(appstream_dir_path);
        }
      } catch (const std::exception& e) {
        spdlog::error("[FlatpakPlugin] Exception processing remote: {}",
                      e.what());
        continue;
      }
    }
    g_ptr_array_unref(remotes);
  }

  const auto id = flatpak_installation_get_id(installation);
  const auto display_name = flatpak_installation_get_display_name(installation);
  const auto installationPath = flatpak_installation_get_path(installation);
  const auto path = g_file_get_path(installationPath);
  const auto no_interaction =
      flatpak_installation_get_no_interaction(installation);
  const auto is_user = flatpak_installation_get_is_user(installation);
  const auto priority = flatpak_installation_get_priority(installation);
  const auto default_languages =
      installation_get_default_languages(installation);
  const auto default_locales = installation_get_default_locales(installation);

  // Validate required fields
  if (!id || !path) {
    spdlog::error("[FlatpakPlugin] Installation missing required fields");
    // Return a valid but minimal Installation object
    return Installation("unknown", "Unknown", "", false, false, 0,
                        flutter::EncodableList{}, flutter::EncodableList{},
                        flutter::EncodableList{});
  }

  // Clean up a path after use
  std::string path_str = path;
  g_free(const_cast<char*>(path));

  return Installation(std::string(id),
                      display_name ? std::string(display_name) : "", path_str,
                      no_interaction, is_user, static_cast<int64_t>(priority),
                      default_languages, default_locales, remote_list);
}

flutter::EncodableMap FlatpakShim::get_content_rating_map(
    FlatpakInstalledRef* ref) {
  flutter::EncodableMap result;
  const auto content_rating =
      flatpak_installed_ref_get_appdata_content_rating(ref);
  if (!content_rating) {
    return result;
  }

  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, content_rating);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    // Validate pointers before casting
    if (key && value) {
      const char* key_str = static_cast<const char*>(key);
      const char* value_str = static_cast<const char*>(value);

      if (key_str && value_str && strlen(key_str) > 0 &&
          strlen(value_str) > 0) {
        result[flutter::EncodableValue(std::string(key_str))] =
            flutter::EncodableValue(std::string(value_str));
      }
    }
  }
  return result;
}

void FlatpakShim::get_application_list(
    FlatpakInstallation* installation,
    flutter::EncodableList& application_list) {
  flutter::EncodableList result;
  GError* error = nullptr;

  const auto refs =
      flatpak_installation_list_installed_refs(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error listing installed refs: {}",
                  error->message);
    g_clear_error(&error);
    return;
  }

  for (guint i = 0; i < refs->len; i++) {
    const auto ref =
        static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

    // Skip non-app refs
    if (flatpak_ref_get_kind(FLATPAK_REF(ref)) != FLATPAK_REF_KIND_APP) {
      continue;
    }

    const auto appdata_name = flatpak_installed_ref_get_appdata_name(ref);
    const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref));
    const auto appdata_summary = flatpak_installed_ref_get_appdata_summary(ref);
    const auto appdata_version = flatpak_installed_ref_get_appdata_version(ref);
    const auto appdata_origin = flatpak_installed_ref_get_origin(ref);
    const auto appdata_license = flatpak_installed_ref_get_appdata_license(ref);
    const auto deploy_dir = flatpak_installed_ref_get_deploy_dir(ref);
    const auto content_rating_type =
        flatpak_installed_ref_get_appdata_content_rating_type(ref);
    const auto latest_commit = flatpak_installed_ref_get_latest_commit(ref);
    const auto eol = flatpak_installed_ref_get_eol(ref);
    const auto eol_rebase = flatpak_installed_ref_get_eol_rebase(ref);

    // Build full application ID
    const auto ref_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
    const auto ref_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));
    std::string full_app_id = "app/";
    full_app_id += (ref_name ? ref_name : "");
    full_app_id += "/";
    full_app_id += (ref_arch ? ref_arch : "");
    full_app_id += "/";
    full_app_id += (ref_branch ? ref_branch : "");

    flutter::EncodableList subpath_list;
    if (const auto subpaths = flatpak_installed_ref_get_subpaths(ref);
        subpaths != nullptr) {
      for (auto sub_path = subpaths; *sub_path != nullptr; ++sub_path) {
        subpath_list.emplace_back(static_cast<const char*>(*sub_path));
      }
    }

    application_list.emplace_back(flutter::CustomEncodableValue(Application(
        appdata_name ? std::string(appdata_name)
                     : (ref_name ? std::string(ref_name) : ""),
        full_app_id, appdata_summary ? std::string(appdata_summary) : "",
        appdata_version ? std::string(appdata_version) : "",
        appdata_origin ? std::string(appdata_origin) : "",
        appdata_license ? std::string(appdata_license) : "",
        static_cast<int64_t>(flatpak_installed_ref_get_installed_size(ref)),
        deploy_dir ? std::string(deploy_dir) : "",
        flatpak_installed_ref_get_is_current(ref),
        content_rating_type ? std::string(content_rating_type) : "",
        get_content_rating_map(ref),
        latest_commit ? std::string(latest_commit) : "",
        eol ? std::string(eol) : "", eol_rebase ? std::string(eol_rebase) : "",
        subpath_list, get_metadata_as_string(ref),
        get_appdata_as_string(ref))));
  }
  g_ptr_array_unref(refs);
}

ErrorOr<flutter::EncodableList> FlatpakShim::GetApplicationsInstalled() {
  flutter::EncodableList application_list;
  GError* error = nullptr;
  auto installation = flatpak_installation_new_user(nullptr, &error);
  get_application_list(installation, application_list);

  const auto system_installations = get_system_installations();
  for (size_t i = 0; i < system_installations->len; i++) {
    installation = static_cast<FlatpakInstallation*>(
        g_ptr_array_index(system_installations, i));
    get_application_list(installation, application_list);
  }
  g_ptr_array_unref(system_installations);

  return ErrorOr<flutter::EncodableList>(std::move(application_list));
}

ErrorOr<Installation> FlatpakShim::GetUserInstallation() {
  GError* error = nullptr;
  const auto installation = flatpak_installation_new_user(nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error getting user installation: {}",
                  error->message);
    g_clear_error(&error);
    return ErrorOr<Installation>(
        FlutterError("INSTALLATION_ERROR", "Failed to get user installation"));
  }

  auto result = get_installation(installation);
  g_object_unref(installation);
  return ErrorOr<Installation>(std::move(result));
}

ErrorOr<flutter::EncodableList> FlatpakShim::GetSystemInstallations() {
  flutter::EncodableList installs_list;
  if (const auto system_installations = get_system_installations()) {
    for (size_t i = 0; i < system_installations->len; i++) {
      const auto installation = static_cast<FlatpakInstallation*>(
          g_ptr_array_index(system_installations, i));
      if (installation) {
        installs_list.emplace_back(
            flutter::CustomEncodableValue(get_installation(installation)));
      }
    }
    g_ptr_array_unref(system_installations);
  }
  return ErrorOr<flutter::EncodableList>(std::move(installs_list));
}

ErrorOr<flutter::EncodableList> FlatpakShim::GetApplicationsRemote(
    const std::string& id) {
  try {
    if (id.empty()) {
      return ErrorOr<flutter::EncodableList>(
          FlutterError("INVALID_REMOTE_GET", "Remote id is required"));
    }

    spdlog::info("[FlatpakPlugin] Get Applications from Remote {}", id);
    GError* error = nullptr;

    auto installation = flatpak_installation_new_user(nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                    error->message);
      g_clear_error(&error);
      return ErrorOr<flutter::EncodableList>(FlutterError(
          "INVALID_REMOTE_GET", "Failed to get user installation"));
    }
    const auto remote = flatpak_installation_get_remote_by_name(
        installation, id.c_str(), nullptr, &error);
    if (!remote) {
      spdlog::error("[FlatpakPlugin] Failed to get remote {}: {}", id.c_str(),
                    error->message);
      g_clear_error(&error);
      g_object_unref(installation);
      return ErrorOr(flutter::EncodableList{});
    }

    auto app_refs = flatpak_installation_list_remote_refs_sync(
        installation, id.c_str(), nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to get applications for remote: {}",
                    error->message);
      g_clear_error(&error);
      g_object_unref(installation);
      return ErrorOr<flutter::EncodableList>(FlutterError(
          "INVALID_REMOTE_GET", "Failed to get remote applications"));
    }

    if (!app_refs) {
      spdlog::error("[FlatpakPlugin] No applications found in remote {}", id);
      g_object_unref(installation);
      g_object_unref(remote);
      g_clear_error(&error);
      return ErrorOr(flutter::EncodableList{});
    }

    flutter::EncodableList application_list =
        convert_applications_to_EncodableList(app_refs, remote);

    g_ptr_array_unref(app_refs);
    g_object_unref(installation);
    spdlog::info("[FlatpakPlugin] Found {} applications in remote {} ",
                 application_list.size(), id);

    return ErrorOr<flutter::EncodableList>(std::move(application_list));
  } catch (const std::exception& e) {
    spdlog::error(
        "[FlatpakPlugin] Exception getting applications from remote: {}",
        e.what());
    return ErrorOr<flutter::EncodableList>(
        FlutterError("INVALID_REMOTE_GET", "Exception occurred"));
  }
}

ErrorOr<bool> FlatpakShim::RemoteAdd(const Remote& configuration) {
  try {
    if (configuration.name().empty() || configuration.url().empty()) {
      return ErrorOr<bool>(FlutterError("INVALID_REMOTE_ADD",
                                        "Remote name and URL are required"));
    }

    spdlog::info("[FlatpakPlugin] Adding Remote {}", configuration.name());
    GError* error = nullptr;
    auto installation = flatpak_installation_new_user(nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                    error->message);
      g_clear_error(&error);
      return ErrorOr<bool>(FlutterError("INVALID_REMOTE_ADD",
                                        "Failed to get user installation"));
    }

    // Check if remote already exists
    auto existing_remote = flatpak_installation_get_remote_by_name(
        installation, configuration.name().c_str(), nullptr, nullptr);
    if (existing_remote) {
      spdlog::warn("[FlatpakPlugin] Remote '{}' already exists",
                   configuration.name());
      g_object_unref(installation);
      g_object_unref(existing_remote);
      return ErrorOr<bool>(
          FlutterError("REMOTE_EXISTS", "Remote already exists"));
    }

    auto remote = flatpak_remote_new(configuration.name().c_str());
    if (!remote) {
      spdlog::error("[FlatpakPlugin] Failed to create remote object");
      g_object_unref(installation);
      return ErrorOr<bool>(
          FlutterError("INVALID_REMOTE_ADD", "Failed to create remote object"));
    }

    // Set required properties
    flatpak_remote_set_url(remote, configuration.url().c_str());

    // Set optional properties
    if (!configuration.title().empty()) {
      flatpak_remote_set_title(remote, configuration.title().c_str());
    }
    if (!configuration.collection_id().empty()) {
      flatpak_remote_set_collection_id(remote,
                                       configuration.collection_id().c_str());
    }
    if (!configuration.comment().empty()) {
      flatpak_remote_set_comment(remote, configuration.comment().c_str());
    }
    if (!configuration.description().empty()) {
      flatpak_remote_set_description(remote,
                                     configuration.description().c_str());
    }
    if (!configuration.default_branch().empty()) {
      flatpak_remote_set_default_branch(remote,
                                        configuration.default_branch().c_str());
    }
    if (!configuration.filter().empty()) {
      flatpak_remote_set_filter(remote, configuration.filter().c_str());
    }
    if (!configuration.homepage().empty()) {
      flatpak_remote_set_homepage(remote, configuration.homepage().c_str());
    }
    if (!configuration.icon().empty()) {
      flatpak_remote_set_icon(remote, configuration.icon().c_str());
    }
    if (!configuration.main_ref().empty()) {
      flatpak_remote_set_main_ref(remote, configuration.main_ref().c_str());
    }

    flatpak_remote_set_nodeps(remote, configuration.no_deps());
    flatpak_remote_set_gpg_verify(remote, configuration.gpg_verify());
    flatpak_remote_set_prio(remote, static_cast<int>(configuration.prio()));
    flatpak_remote_set_disabled(remote, configuration.disabled());

    gboolean result = flatpak_installation_add_remote(installation, remote,
                                                      TRUE, nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to add remote: {}", error->message);
      g_object_unref(installation);
      g_object_unref(remote);
      g_clear_error(&error);
      return ErrorOr<bool>(
          FlutterError("INVALID_REMOTE_ADD", "Failed to add remote"));
    }

    if (result) {
      spdlog::info("[FlatpakPlugin] Remote '{}' added successfully",
                   configuration.name());
    }

    g_object_unref(installation);
    g_object_unref(remote);
    return ErrorOr<bool>(static_cast<bool>(result));
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception adding remote: {}", e.what());
    return ErrorOr<bool>(
        FlutterError("INVALID_REMOTE_ADD", "Exception occurred"));
  }
}

ErrorOr<bool> FlatpakShim::RemoteRemove(const std::string& id) {
  try {
    if (id.empty()) {
      return ErrorOr<bool>(
          FlutterError("INVALID_REMOTE_REMOVE", "Remote ID is required"));
    }

    spdlog::info("[FlatpakPlugin] Removing remote {}", id);
    GError* error = nullptr;
    auto installation = flatpak_installation_new_user(nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                    error->message);
      g_clear_error(&error);
      return ErrorOr<bool>(FlutterError("INVALID_REMOTE_REMOVE",
                                        "Failed to get user installation"));
    }

    gboolean result = flatpak_installation_remove_remote(
        installation, id.c_str(), nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to remove remote: {}",
                    error->message);
      g_object_unref(installation);
      g_clear_error(&error);
      return ErrorOr<bool>(
          FlutterError("INVALID_REMOTE_REMOVE", "Failed to remove remote"));
    }

    if (result) {
      spdlog::info("[FlatpakPlugin] Remote '{}' removed successfully", id);
    }

    g_object_unref(installation);
    return ErrorOr<bool>(static_cast<bool>(result));
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception removing remote: {}", e.what());
    return ErrorOr<bool>(
        FlutterError("INVALID_REMOTE_REMOVE", "Exception occurred"));
  }
}

ErrorOr<bool> FlatpakShim::ApplicationInstall(const std::string& id) {
  try {
    if (id.empty()) {
      return ErrorOr<bool>(
          FlutterError("INVALID_APP_ID", "Application ID is required"));
    }

    spdlog::debug("[FlatpakPlugin] Installing application: {}", id);
    GError* error = nullptr;

    auto installation = flatpak_installation_new_user(nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                    error->message);
      g_clear_error(&error);
      return ErrorOr<bool>(FlutterError("INSTALLATION_ERROR",
                                        "Failed to get user installation"));
    }

    // Try parsing as a full ref first
    auto app_ref = flatpak_ref_parse(id.c_str(), &error);
    if (!error && app_ref) {
      const char* app_name = flatpak_ref_get_name(app_ref);
      const char* app_arch = flatpak_ref_get_arch(app_ref);
      const char* app_branch = flatpak_ref_get_branch(app_ref);

      spdlog::debug(
          "[FlatpakPlugin] Parsed ref - name: {}, arch: {}, branch: {}",
          app_name, app_arch, app_branch);

      std::string remote_name =
          find_remote_for_app(installation, app_name, app_arch, app_branch);
      if (remote_name.empty()) {
        spdlog::error("[FlatpakPlugin] Failed to find remote for app: {}",
                      app_name);
        g_object_unref(app_ref);
        g_object_unref(installation);
        return ErrorOr<bool>(
            FlutterError("INSTALL_FAILED", "Failed to find remote for app"));
      }

      FlatpakInstalledRef* installed_ref = flatpak_installation_install(
          installation, remote_name.c_str(), FLATPAK_REF_KIND_APP, app_name,
          app_arch, app_branch, nullptr, nullptr, nullptr, &error);

      if (error) {
        spdlog::error("[FlatpakPlugin] Failed to install '{}': {}", id,
                      error->message);
        g_object_unref(app_ref);
        g_object_unref(installation);
        g_clear_error(&error);
        return ErrorOr<bool>(
            FlutterError("INSTALL_FAILED", "Failed to install application"));
      }

      if (installed_ref) {
        spdlog::info("[FlatpakPlugin] Successfully installed: {}", id);
        g_object_unref(installed_ref);
      }
      g_object_unref(app_ref);
      g_object_unref(installation);
      return ErrorOr<bool>(true);
    }

    if (error) {
      g_clear_error(&error);
    }

    // Search in all remotes for this app ID
    auto remote_and_ref = find_app_in_remotes(installation, id);
    if (remote_and_ref.first.empty()) {
      spdlog::error("[FlatpakPlugin] Application '{}' not found in any remote",
                    id);
      g_object_unref(installation);
      return ErrorOr<bool>(
          FlutterError("APP_NOT_FOUND", "Application not found in remotes"));
    }

    auto found_ref = flatpak_ref_parse(remote_and_ref.second.c_str(), &error);
    if (error || !found_ref) {
      spdlog::error("[FlatpakPlugin] Failed to parse found ref: {}",
                    remote_and_ref.second);
      g_object_unref(installation);
      if (error) {
        g_clear_error(&error);
      }
      return ErrorOr<bool>(
          FlutterError("APP_NOT_FOUND", "Failed to parse found ref"));
    }

    const char* app_name = flatpak_ref_get_name(found_ref);
    const char* app_arch = flatpak_ref_get_arch(found_ref);
    const char* app_branch = flatpak_ref_get_branch(found_ref);

    spdlog::info("[FlatpakPlugin] Found app '{}' in remote '{}' as '{}'", id,
                 remote_and_ref.first, remote_and_ref.second);

    FlatpakInstalledRef* installed_ref = flatpak_installation_install(
        installation, remote_and_ref.first.c_str(), FLATPAK_REF_KIND_APP,
        app_name, app_arch, app_branch, nullptr, nullptr, nullptr, &error);

    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to install '{}': {}", id,
                    error->message);
      g_object_unref(found_ref);
      g_object_unref(installation);
      g_clear_error(&error);
      return ErrorOr<bool>(
          FlutterError("INSTALL_FAILED", "Failed to install application"));
    }

    if (installed_ref) {
      spdlog::info("[FlatpakPlugin] Successfully installed: {}", id);
      g_object_unref(installed_ref);
    }

    g_object_unref(found_ref);
    g_object_unref(installation);
    return ErrorOr<bool>(true);
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception installing application: {}",
                  e.what());
    return ErrorOr<bool>(FlutterError("INSTALL_FAILED", "Exception occurred"));
  }
}

ErrorOr<bool> FlatpakShim::ApplicationUninstall(const std::string& id) {
  try {
    if (id.empty()) {
      return ErrorOr<bool>(
          FlutterError("INVALID_APP_ID", "Application ID is required"));
    }

    spdlog::debug("[FlatpakPlugin] Uninstalling application: {}", id);
    GError* error = nullptr;

    auto installation = flatpak_installation_new_user(nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                    error->message);
      g_clear_error(&error);
      return ErrorOr<bool>(FlutterError("INSTALLATION_ERROR",
                                        "Failed to get user installation"));
    }

    // Try parsing as a full ref first
    auto app_ref = flatpak_ref_parse(id.c_str(), &error);
    if (!error && app_ref) {
      const char* app_name = flatpak_ref_get_name(app_ref);
      const char* app_arch = flatpak_ref_get_arch(app_ref);
      const char* app_branch = flatpak_ref_get_branch(app_ref);

      spdlog::debug(
          "[FlatpakPlugin] Parsed ref - name: {}, arch: {}, branch: {}",
          app_name, app_arch, app_branch);

      gboolean result = flatpak_installation_uninstall(
          installation, FLATPAK_REF_KIND_APP, app_name, app_arch, app_branch,
          nullptr, nullptr, nullptr, &error);

      if (error) {
        spdlog::error("[FlatpakPlugin] Failed to uninstall '{}': {}", id,
                      error->message);
        g_object_unref(app_ref);
        g_object_unref(installation);
        g_clear_error(&error);
        return ErrorOr<bool>(FlutterError("UNINSTALL_FAILED",
                                          "Failed to uninstall application"));
      }

      if (result) {
        spdlog::info("[FlatpakPlugin] Successfully uninstalled: {}", id);
      }
      g_object_unref(app_ref);
      g_object_unref(installation);
      return ErrorOr(static_cast<bool>(result));
    }

    if (error) {
      g_clear_error(&error);
    }

    // Search in installed apps
    spdlog::debug("[FlatpakPlugin] Searching installed apps for: {}", id);

    const auto refs =
        flatpak_installation_list_installed_refs(installation, nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to get installed apps: {}",
                    error->message);
      g_clear_error(&error);
      g_object_unref(installation);
      return ErrorOr<bool>(
          FlutterError("UNINSTALL_ERROR", "Failed to get installed apps"));
    }

    std::string found_app_name;
    std::string found_arch;
    std::string found_branch;
    bool app_found = false;

    // Search for exact match first, then partial match
    for (guint i = 0; i < refs->len && !app_found; i++) {
      const auto ref =
          static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

      if (flatpak_ref_get_kind(FLATPAK_REF(ref)) != FLATPAK_REF_KIND_APP) {
        continue;
      }

      if (const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref))) {
        std::string ref_name_str(ref_name);
        // Check for exact match
        if (ref_name_str == id) {
          found_app_name = ref_name_str;
          found_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
          found_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));
          app_found = true;
          spdlog::debug("[FlatpakPlugin] Found exact match: {}", ref_name_str);
        }
      }
    }

    // If no exact match, try partial match
    if (!app_found) {
      for (guint i = 0; i < refs->len; i++) {
        const auto ref =
            static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

        if (flatpak_ref_get_kind(FLATPAK_REF(ref)) != FLATPAK_REF_KIND_APP) {
          continue;
        }

        if (const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref))) {
          if (std::string ref_name_str(ref_name);
              ref_name_str.find(id) != std::string::npos) {
            found_app_name = ref_name_str;
            found_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
            found_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));
            app_found = true;
            spdlog::debug("[FlatpakPlugin] Found partial match: {}",
                          ref_name_str);
            break;
          }
        }
      }
    }

    g_ptr_array_unref(refs);

    if (!app_found) {
      spdlog::error(
          "[FlatpakPlugin] Application '{}' not found in installed "
          "applications",
          id);
      g_object_unref(installation);
      return ErrorOr<bool>(
          FlutterError("APP_NOT_FOUND", "Application not found"));
    }

    spdlog::info(
        "[FlatpakPlugin] Found installed app '{}' -> name: {}, arch: {}, "
        "branch: {}",
        id, found_app_name, found_arch, found_branch);

    gboolean result = flatpak_installation_uninstall(
        installation, FLATPAK_REF_KIND_APP, found_app_name.c_str(),
        found_arch.c_str(), found_branch.c_str(), nullptr, nullptr, nullptr,
        &error);

    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to uninstall '{}': {}", id,
                    error->message);
      g_object_unref(installation);
      g_clear_error(&error);
      return ErrorOr<bool>(
          FlutterError("UNINSTALL_FAILED", "Failed to uninstall application"));
    }

    if (result) {
      spdlog::info("[FlatpakPlugin] Successfully uninstalled: {}", id);
    }
    g_object_unref(installation);
    return ErrorOr<bool>(static_cast<bool>(result));
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception uninstalling application: {}",
                  e.what());
    return ErrorOr<bool>(
        FlutterError("UNINSTALL_FAILED", "Exception occurred"));
  }
}

ErrorOr<bool> FlatpakShim::ApplicationStart(
    const std::string& id,
    const asio::io_context::strand& strand) {
  if (id.empty()) {
    return ErrorOr<bool>(
        FlutterError("INVALID_APP_ID", "Application ID is required"));
  }

  spdlog::debug("[FlatpakPlugin] Starting application: {}", id);

  // Check if the application is installed first
  FlatpakInstalledRef* installed = nullptr;
  GError* error = nullptr;
  auto installation = flatpak_installation_new_user(nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                  error->message);
    g_clear_error(&error);
    return ErrorOr<bool>(
        FlutterError("INSTALLATION_ERROR", "Failed to get user installation"));
  }

  const auto refs =
      flatpak_installation_list_installed_refs(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Failed to get installed apps: {}",
                  error->message);
    g_clear_error(&error);
    g_object_unref(installation);
    return ErrorOr<bool>(
        FlutterError("START_FAILED", "Failed to get installed apps"));
  }

  bool app_found = false;
  std::string found_app_name;
  std::string found_arch;
  std::string found_branch;

  // Search for the application
  for (guint i = 0; i < refs->len && !app_found; i++) {
    const auto ref =
        static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

    if (flatpak_ref_get_kind(FLATPAK_REF(ref)) != FLATPAK_REF_KIND_APP) {
      continue;
    }

    if (const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref))) {
      std::string ref_name_str(ref_name);
      const auto ref_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
      const auto ref_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));

      std::string full_app_id = "app/";
      full_app_id += ref_name_str + "/";
      full_app_id += (ref_arch ? ref_arch : "");
      full_app_id += "/";
      full_app_id += (ref_branch ? ref_branch : "");

      if (full_app_id == id || ref_name_str == id) {
        found_app_name = ref_name_str;
        found_arch = ref_arch ? ref_arch : "";
        found_branch = ref_branch ? ref_branch : "";
        app_found = true;
        g_object_ref(ref);
        installed = ref;
        break;
      }
    }
  }

  g_ptr_array_unref(refs);

  if (!app_found) {
    spdlog::error("[FlatpakPlugin] Application '{}' not found", id);
    g_object_unref(installation);
    return ErrorOr<bool>(
        FlutterError("APP_NOT_FOUND", "Application not found"));
  }

  spdlog::info(
      "[FlatpakPlugin] Would start application: {} (name: {}, arch: {}, "
      "branch: {})",
      id, found_app_name, found_arch, found_branch);

  create_sandbox(
      installed, strand,
      [installation, found_app_name, found_arch, found_branch,
       installed](bool success) {
        if (!success) {
          spdlog::error("[FlatpakPlugin] Failed to start application");
          g_object_unref(installed);
          g_object_unref(installation);
          return;
        }
        FlatpakInstance* instance = nullptr;
        GError* error = nullptr;
        flatpak_installation_launch_full(
            installation, FLATPAK_LAUNCH_FLAGS_DO_NOT_REAP,
            found_app_name.c_str(), found_arch.c_str(), found_branch.c_str(),
            nullptr, &instance, nullptr, &error);
        if (error) {
          spdlog::error("[FlatpakPlugin] Failed to run app: {}",
                        error->message);
          g_clear_error(&error);
          g_object_unref(installation);
        }
        if (instance) {
          spdlog::info(
              "[FlatpakPlugin] Successfully launched application: {} (PID: {})",
              found_app_name, flatpak_instance_get_pid(instance));
          Instance app;
          app.instance = instance;
          app.app_id = found_app_name;

          const pid_t pid = flatpak_instance_get_pid(instance);
          app.child_id = pid;
          instances[found_app_name] = app;
        }
        g_object_unref(installed);
        g_object_unref(installation);
      });

  return ErrorOr<bool>(true);
}

ErrorOr<bool> FlatpakShim::ApplicationStop(const std::string& id) {
  if (id.empty()) {
    return ErrorOr<bool>(
        FlutterError("INVALID_APP_ID", "Application ID is required"));
  }

  spdlog::info("[FlatpakPlugin] Stopping application: {}", id);
  auto app = instances.find(id);
  if (app == instances.end()) {
    return ErrorOr<bool>(false);
  }

  FlatpakInstance* instance = app->second.instance;

  const pid_t child_pid = flatpak_instance_get_child_pid(instance);
  if (child_pid > 0) {
    spdlog::info("[FlatpakPlugin] Sending SIGTERM to child PID: {}", child_pid);
    if (kill(child_pid, SIGTERM) != 0) {
      spdlog::error("[FlatpakPlugin] Failed to send SIGTERM: {}",
                    strerror(errno));
    }

    bool exited = false;
    for (int i = 0; i < 50; i++) {
      if (kill(child_pid, 0) != 0) {
        exited = true;
        spdlog::info("[FlatpakPlugin] App exited gracefully: {}", id);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!exited) {
      spdlog::error("[FlatpakPlugin] Forcing kill of entire process group");
      kill(child_pid, SIGKILL);
    }
  }
  g_object_unref(instance);
  instances.erase(app);
  spdlog::info("[FlatpakPlugin] App stopped and cleaned up: {}", id);
  return ErrorOr<bool>(true);
}

ErrorOr<flutter::EncodableList> FlatpakShim::get_remotes_by_installation_id(
    const std::string& installation_id) {
  try {
    FlatpakInstallation* installation = nullptr;
    GError* error = nullptr;

    if (installation_id == "user") {
      installation = flatpak_installation_new_user(nullptr, &error);
      if (error) {
        const std::string error_msg = error->message;
        g_clear_error(&error);
        return ErrorOr<flutter::EncodableList>(
            FlutterError("INSTALLATION_ERROR",
                         "Failed to get user installation: " + error_msg));
      }
    } else {
      // Check system installations
      const auto system_installations = get_system_installations();
      if (!system_installations) {
        return ErrorOr<flutter::EncodableList>(
            FlutterError("NO_INSTALLATIONS", "No system installations found"));
      }

      for (size_t i = 0; i < system_installations->len; i++) {
        auto* sys_installation = static_cast<FlatpakInstallation*>(
            g_ptr_array_index(system_installations, i));

        if (const auto id = flatpak_installation_get_id(sys_installation);
            id && installation_id == std::string(id)) {
          installation =
              static_cast<FlatpakInstallation*>(g_object_ref(sys_installation));
          break;
        }
      }

      g_ptr_array_unref(system_installations);

      if (!installation) {
        return ErrorOr<flutter::EncodableList>(FlutterError(
            "INSTALLATION_NOT_FOUND",
            "Installation with ID '" + installation_id + "' not found"));
      }
    }

    if (!installation) {
      return ErrorOr<flutter::EncodableList>(
          FlutterError("INSTALLATION_ERROR", "Failed to get installation"));
    }

    GPtrArray* remotes = get_remotes(installation);
    if (!remotes) {
      g_object_unref(installation);
      return ErrorOr(flutter::EncodableList{});
    }

    auto remote_list = convert_remotes_to_EncodableList(remotes);
    g_ptr_array_unref(remotes);
    g_object_unref(installation);

    spdlog::debug(
        "[FlatpakPlugin] Successfully retrieved {} remotes for installation {}",
        remote_list.size(), installation_id);
    return ErrorOr(std::move(remote_list));
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception getting remotes: {}", e.what());
    return ErrorOr<flutter::EncodableList>(FlutterError(
        "UNKNOWN_ERROR", "Failed to get remotes: " + std::string(e.what())));
  }
}

std::string FlatpakShim::find_remote_for_app(FlatpakInstallation* installation,
                                             const char* app_name,
                                             const char* app_arch,
                                             const char* app_branch) {
  GError* error = nullptr;
  auto remotes =
      flatpak_installation_list_remotes(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error getting remotes: {}", error->message);
    g_clear_error(&error);
    return "";
  }

  for (guint i = 0; i < remotes->len; i++) {
    auto remote = static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, i));
    const auto remote_name = flatpak_remote_get_name(remote);

    if (flatpak_remote_get_disabled(remote)) {
      continue;
    }

    auto remote_ref = flatpak_installation_fetch_remote_ref_sync(
        installation, remote_name, FLATPAK_REF_KIND_APP, app_name, app_arch,
        app_branch, nullptr, &error);

    if (remote_ref && !error) {
      g_object_unref(remote_ref);
      g_ptr_array_unref(remotes);
      return {remote_name};
    }

    if (error) {
      g_clear_error(&error);
    }
  }

  g_ptr_array_unref(remotes);
  return "";
}

std::pair<std::string, std::string> FlatpakShim::find_app_in_remotes(
    FlatpakInstallation* installation,
    const std::string& app_id) {
  std::vector<std::string> priority_remotes = {"flathub", "fedora",
                                               "gnome-nightly"};
  std::vector<std::string> common_branches = {"stable", "beta", "master"};
  std::string default_arch = flatpak_get_default_arch();

  // Try priority remotes first
  for (const auto& remote_name : priority_remotes) {
    for (const auto& branch_name : common_branches) {
      GError* error = nullptr;
      auto remote_ref = flatpak_installation_fetch_remote_ref_sync(
          installation, remote_name.c_str(), FLATPAK_REF_KIND_APP,
          app_id.c_str(), default_arch.c_str(), branch_name.c_str(), nullptr,
          &error);

      if (remote_ref && !error) {
        std::string ref_string = "app/";
        ref_string += app_id;
        ref_string += "/";
        ref_string += default_arch;
        ref_string += "/";
        ref_string += branch_name;

        spdlog::info("[FlatpakPlugin] Found '{}' in remote '{}' as '{}'",
                     app_id, remote_name, ref_string);
        g_object_unref(remote_ref);
        return {remote_name, ref_string};
      }

      if (error) {
        g_clear_error(&error);
      }
    }
  }

  return find_app_in_remotes_fallback(installation, app_id);
}

std::pair<std::string, std::string> FlatpakShim::find_app_in_remotes_fallback(
    FlatpakInstallation* installation,
    const std::string& app_id) {
  GError* error = nullptr;
  auto remotes =
      flatpak_installation_list_remotes(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error getting remotes: {}", error->message);
    g_clear_error(&error);
    return {"", ""};
  }

  for (guint i = 0; i < remotes->len; i++) {
    auto remote = static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, i));
    if (flatpak_remote_get_disabled(remote)) {
      continue;
    }

    const auto remote_name = flatpak_remote_get_name(remote);
    auto result = search_in_single_remote(installation, remote_name, app_id);
    if (!result.first.empty()) {
      g_ptr_array_unref(remotes);
      return result;
    }
  }

  g_ptr_array_unref(remotes);
  return {"", ""};
}

std::pair<std::string, std::string> FlatpakShim::search_in_single_remote(
    FlatpakInstallation* installation,
    const char* remote_name,
    const std::string& app_id) {
  GError* error = nullptr;
  auto remote_refs = flatpak_installation_list_remote_refs_sync(
      installation, remote_name, nullptr, &error);

  if (error) {
    spdlog::error("[FlatpakPlugin] Skipping remote '{}': {}", remote_name,
                  error->message);
    g_clear_error(&error);
    return {"", ""};
  }

  for (guint j = 0; j < remote_refs->len; j++) {
    auto remote_ref =
        static_cast<FlatpakRemoteRef*>(g_ptr_array_index(remote_refs, j));

    if (flatpak_ref_get_kind(FLATPAK_REF(remote_ref)) != FLATPAK_REF_KIND_APP) {
      continue;
    }

    const char* ref_name = flatpak_ref_get_name(FLATPAK_REF(remote_ref));
    if (ref_name && app_id == std::string(ref_name)) {
      std::string ref_string = std::string("app/") + ref_name + "/" +
                               flatpak_ref_get_arch(FLATPAK_REF(remote_ref)) +
                               "/" +
                               flatpak_ref_get_branch(FLATPAK_REF(remote_ref));

      spdlog::info("[FlatpakPlugin] Found '{}' in remote '{}' as '{}'", app_id,
                   remote_name, ref_string);
      g_ptr_array_unref(remote_refs);
      return {std::string(remote_name), ref_string};
    }
  }

  g_ptr_array_unref(remote_refs);
  return {"", ""};
}

flutter::EncodableList FlatpakShim::convert_remotes_to_EncodableList(
    const GPtrArray* remotes) {
  flutter::EncodableList result;

  if (!remotes) {
    spdlog::error("[FlatpakPlugin] Received null remotes array");
    return result;
  }

  for (size_t j = 0; j < remotes->len; j++) {
    const auto remote =
        static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, j));
    if (!remote) {
      spdlog::error("[FlatpakPlugin] Null remote at index {}", j);
      continue;
    }

    try {
      const auto name = flatpak_remote_get_name(remote);
      const auto url = flatpak_remote_get_url(remote);

      // Skip remotes without required fields
      if (!name || !url) {
        continue;
      }

      const auto collection_id = flatpak_remote_get_collection_id(remote);
      const auto title = flatpak_remote_get_title(remote);
      const auto comment = flatpak_remote_get_comment(remote);
      const auto description = flatpak_remote_get_description(remote);
      const auto homepage = flatpak_remote_get_homepage(remote);
      const auto icon = flatpak_remote_get_icon(remote);
      const auto default_branch = flatpak_remote_get_default_branch(remote);
      const auto main_ref = flatpak_remote_get_main_ref(remote);
      const auto filter = flatpak_remote_get_filter(remote);
      const bool gpg_verify = flatpak_remote_get_gpg_verify(remote);
      const bool no_enumerate = flatpak_remote_get_noenumerate(remote);
      const bool no_deps = flatpak_remote_get_nodeps(remote);
      const bool disabled = flatpak_remote_get_disabled(remote);
      const int32_t prio = flatpak_remote_get_prio(remote);

      const auto default_arch = flatpak_get_default_arch();
      auto appstream_timestamp_path = g_file_get_path(
          flatpak_remote_get_appstream_timestamp(remote, default_arch));
      auto appstream_dir_path = g_file_get_path(
          flatpak_remote_get_appstream_dir(remote, default_arch));

      auto appstream_timestamp = get_appstream_timestamp(
          appstream_timestamp_path ? appstream_timestamp_path : "");
      char formatted_time[64] = {0};
      format_time_iso8601(appstream_timestamp, formatted_time,
                          sizeof(formatted_time));

      result.emplace_back(flutter::CustomEncodableValue(Remote(
          std::string(name), std::string(url),
          collection_id ? std::string(collection_id) : "",
          title ? std::string(title) : "", comment ? std::string(comment) : "",
          description ? std::string(description) : "",
          homepage ? std::string(homepage) : "", icon ? std::string(icon) : "",
          default_branch ? std::string(default_branch) : "",
          main_ref ? std::string(main_ref) : "",
          FlatpakRemoteTypeToString(flatpak_remote_get_remote_type(remote)),
          filter ? std::string(filter) : "", std::string(formatted_time),
          appstream_dir_path ? std::string(appstream_dir_path) : "", gpg_verify,
          no_enumerate, no_deps, disabled, static_cast<int64_t>(prio))));

      if (appstream_timestamp_path) {
        g_free(appstream_timestamp_path);
      }

      if (appstream_dir_path) {
        g_free(appstream_dir_path);
      }
    } catch (const std::exception& e) {
      spdlog::error("[FlatpakPlugin] Received exception {}", e.what());
    }
  }
  spdlog::debug("[FlatpakPlugin] Converted {} remotes to encodable list ",
                result.size());
  return result;
}

flutter::EncodableList FlatpakShim::convert_applications_to_EncodableList(
    const GPtrArray* applications,
    FlatpakRemote* remote) {
  auto result = flutter::EncodableList();
  if (!applications) {
    spdlog::error("[FlatpakPlugin] Converter received null apps array");
    return result;
  }

  const auto default_arch = flatpak_get_default_arch();
  auto appstream_dir = flatpak_remote_get_appstream_dir(remote, default_arch);
  std::optional<AppstreamCatalog> catalog;
  if (appstream_dir) {
    if (auto appstream_path = g_file_get_path(appstream_dir)) {
      const std::string appstream_file =
          std::string(appstream_path) + "/appstream.xml.gz";
      catalog = AppstreamCatalog(appstream_file, "en");
      spdlog::debug("[FlatpakPlugin] AppstreamCatalog loaded {} components",
                    catalog->getTotalComponentCount());
      g_free(appstream_path);
    }
  }

  result.reserve(applications->len);
  for (size_t j = 0; j < applications->len; j++) {
    const auto app_ref =
        static_cast<FlatpakRemoteRef*>(g_ptr_array_index(applications, j));
    if (!app_ref) {
      spdlog::error("[FlatpakPlugin] null application at index {}", j);
      continue;
    }

    if (flatpak_ref_get_kind(FLATPAK_REF(app_ref)) != FLATPAK_REF_KIND_APP) {
      continue;
    }
    try {
      const auto name = flatpak_ref_get_name(FLATPAK_REF(app_ref));
      if (!name) {
        spdlog::error(
            "[FlatpakPlugin] Application at index {} has no name, skipping", j);
        continue;
      }

      auto app_component = create_component(app_ref, catalog);
      if (app_component.has_value()) {
        result.emplace_back(
            flutter::CustomEncodableValue(app_component.value()));
      }
    } catch (const std::exception& e) {
      spdlog::error("[FlatpakPlugin] Received exception {}", e.what());
    }
  }
  spdlog::debug("[FlatpakPlugin] Converted {} applications to encodable list",
                result.size());
  return result;
}

std::string FlatpakShim::get_metadata_as_string(
    FlatpakInstalledRef* installed_ref) {
  GError* error = nullptr;

  const auto g_bytes =
      flatpak_installed_ref_load_metadata(installed_ref, nullptr, &error);

  if (!g_bytes) {
    if (error != nullptr) {
      spdlog::error("[FlatpakPlugin] Error loading metadata: {}\n",
                    error->message);
      g_clear_error(&error);
    }
    return {};
  }

  gsize size;
  const auto data = static_cast<const char*>(g_bytes_get_data(g_bytes, &size));
  std::string result(data, size);
  g_bytes_unref(g_bytes);
  return result;
}

std::string FlatpakShim::get_appdata_as_string(
    FlatpakInstalledRef* installed_ref) {
  GError* error = nullptr;
  const auto g_bytes =
      flatpak_installed_ref_load_appdata(installed_ref, nullptr, &error);
  if (!g_bytes) {
    if (error != nullptr) {
      SPDLOG_ERROR("[FlatpakPlugin] {}", error->message);
      g_clear_error(&error);
    }
    return {};
  }

  gsize size;
  const auto data =
      static_cast<const uint8_t*>(g_bytes_get_data(g_bytes, &size));
  const std::vector<char> compressedData(data, data + size);
  std::vector<char> decompressedData;
  decompress_gzip(compressedData, decompressedData);
  std::string decompressedString(decompressedData.begin(),
                                 decompressedData.end());
  return decompressedString;
}

std::vector<char> FlatpakShim::decompress_gzip(
    const std::vector<char>& compressedData,
    std::vector<char>& decompressedData) {
  z_stream zs = {};
  if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
    spdlog::error("[FlatpakPlugin] Unable to initialize zlib inflate");
    return {};
  }
  zs.next_in =
      reinterpret_cast<Bytef*>(const_cast<char*>(compressedData.data()));
  zs.avail_in = static_cast<uInt>(compressedData.size());
  int zlibResult;
  char buffer[BUFFER_SIZE];
  do {
    zs.next_out = reinterpret_cast<Bytef*>(buffer);
    zs.avail_out = BUFFER_SIZE;
    zlibResult = inflate(&zs, 0);
    decompressedData.insert(decompressedData.end(), buffer,
                            buffer + BUFFER_SIZE - zs.avail_out);
  } while (zlibResult == Z_OK);
  inflateEnd(&zs);
  if (zlibResult != Z_STREAM_END) {
    spdlog::error("[FlatpakPlugin] Gzip decompression error");
    return {};
  }
  return decompressedData;
}

std::optional<Application> FlatpakShim::create_component(
    FlatpakRemoteRef* app_ref,
    const std::optional<AppstreamCatalog>& app_catalog) {
  if (!app_ref) {
    spdlog::error("[FlatpakPlugin] Cannot create component from null");
    return std::nullopt;
  }

  try {
    const char* app_id = flatpak_ref_get_name(FLATPAK_REF(app_ref));
    const char* remote_name = flatpak_remote_ref_get_remote_name(app_ref);
    const char* eol = flatpak_remote_ref_get_eol(app_ref);
    const char* eol_rebase = flatpak_remote_ref_get_eol_rebase(app_ref);
    const guint64 installed_size =
        flatpak_remote_ref_get_installed_size(app_ref);

    std::string name = app_id;
    std::string summary;
    std::string version;
    std::string license;
    std::string content_rating_type;
    flutter::EncodableMap content_rating;
    std::string metadata;
    std::string appdata;

    // fill Application data fields from catalog
    if (app_catalog.has_value()) {
      auto component_optional = app_catalog->searchById(std::string(app_id));
      if (component_optional.has_value()) {
        const auto& component = component_optional.value();
        name = component.getName();
        summary = component.getSummary();
        if (component.getVersion().has_value()) {
          version = component.getVersion().value();
        }
        if (component.getProjectLicense().has_value()) {
          license = component.getProjectLicense().value();
        }
        if (component.getContentRatingType().has_value()) {
          content_rating_type = component.getContentRatingType().value();
        }
        if (component.getContentRating().has_value()) {
          const auto& rating_map = component.getContentRating().value();
          for (const auto& [key, val] : rating_map) {
            content_rating[flutter::EncodableValue(key)] =
                flutter::EncodableValue(Component::RatingValueToString(val));
          }
        }
        metadata = create_metadata(component);
        appdata = create_appdata(component);
      }
    }
    flutter::EncodableList subpaths;
    return Application(
        name, app_id ? std::string(app_id) : "", summary, version,
        remote_name ? std::string(remote_name) : "", license,
        static_cast<int64_t>(installed_size), "", false, content_rating_type,
        content_rating, "", eol ? std::string(eol) : "",
        eol_rebase ? std::string(eol_rebase) : "", subpaths, metadata, appdata);
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Component creation failed for : {}",
                  e.what());
    return std::nullopt;
  }
}

std::string FlatpakShim::create_metadata(const Component& component) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

  document.AddMember(
      "id", rapidjson::Value(component.getId().c_str(), allocator), allocator);
  document.AddMember(
      "pkgname", rapidjson::Value(component.getPkgName().c_str(), allocator),
      allocator);
  if (component.getVersion().has_value()) {
    document.AddMember(
        "version",
        rapidjson::Value(component.getVersion().value().c_str(), allocator),
        allocator);
  }
  if (component.getArchitecture().has_value()) {
    document.AddMember(
        "architecture",
        rapidjson::Value(component.getArchitecture().value().c_str(),
                         allocator),
        allocator);
  }
  if (component.getBundle().has_value()) {
    document.AddMember(
        "bundle",
        rapidjson::Value(component.getBundle().value().c_str(), allocator),
        allocator);
  }
  if (component.getSourcePkgname().has_value()) {
    document.AddMember(
        "sourcepkgname",
        rapidjson::Value(component.getSourcePkgname().value().c_str(),
                         allocator),
        allocator);
  }
  if (component.getUrl().has_value()) {
    document.AddMember(
        "url", rapidjson::Value(component.getUrl().value().c_str(), allocator),
        allocator);
  }
  if (component.getMediaBaseurl().has_value()) {
    document.AddMember(
        "mediabaseurl",
        rapidjson::Value(component.getMediaBaseurl().value().c_str(),
                         allocator),
        allocator);
  }
  if (component.getCategories().has_value()) {
    rapidjson::Value categoriesArray(rapidjson::kArrayType);
    for (const auto& c : component.getCategories().value()) {
      categoriesArray.PushBack(rapidjson::Value(c.c_str(), allocator),
                               allocator);
    }
    document.AddMember("categories", categoriesArray, allocator);
  }
  if (component.getKeywords().has_value()) {
    rapidjson::Value keywordsArray(rapidjson::kArrayType);
    for (const auto& k : component.getKeywords().value()) {
      keywordsArray.PushBack(rapidjson::Value(k.c_str(), allocator), allocator);
    }
    document.AddMember("keywords", keywordsArray, allocator);
  }
  if (component.getLanguages().has_value()) {
    rapidjson::Value languagesArray(rapidjson::kArrayType);
    for (const auto& l : component.getLanguages().value()) {
      languagesArray.PushBack(rapidjson::Value(l.c_str(), allocator),
                              allocator);
    }
    document.AddMember("languages", languagesArray, allocator);
  }
  if (component.getDeveloper().has_value()) {
    rapidjson::Value developerArray(rapidjson::kArrayType);
    for (const auto& d : component.getDeveloper().value()) {
      developerArray.PushBack(rapidjson::Value(d.c_str(), allocator),
                              allocator);
    }
    document.AddMember("developer", developerArray, allocator);
  }

  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  return buffer.GetString();
}

std::string FlatpakShim::create_appdata(const Component& component) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

  // description
  if (component.getDescription().has_value()) {
    document.AddMember(
        "description",
        rapidjson::Value(component.getDescription().value().c_str(), allocator),
        allocator);
  }

  // screenshots
  if (component.getScreenshots().has_value()) {
    rapidjson::Value screenshotsArray(rapidjson::kArrayType);
    for (const auto& s : component.getScreenshots().value()) {
      rapidjson::Value screenshot(rapidjson::kObjectType);
      if (s.getType().has_value()) {
        screenshot.AddMember(
            "type", rapidjson::Value(s.getType().value().c_str(), allocator),
            allocator);
      }

      if (!s.getCaptions().empty()) {
        rapidjson::Value caption(rapidjson::kArrayType);
        for (const auto& c : s.getCaptions()) {
          caption.PushBack(rapidjson::Value(c.c_str(), allocator), allocator);
        }
        screenshot.AddMember("captions", caption, allocator);
      }

      if (s.getImages().has_value()) {
        rapidjson::Value imagesArray(rapidjson::kArrayType);
        for (const auto& i : s.getImages().value()) {
          rapidjson::Value image(rapidjson::kObjectType);
          if (i.getType().has_value()) {
            image.AddMember(
                "type",
                rapidjson::Value(i.getType().value().c_str(), allocator),
                allocator);
          }
          if (i.getUrl().has_value()) {
            image.AddMember(
                "url", rapidjson::Value(i.getUrl().value().c_str(), allocator),
                allocator);
          }
          if (i.getWidth().has_value()) {
            image.AddMember(
                "width",
                rapidjson::Value(std::to_string(i.getWidth().value()).c_str(),
                                 allocator),
                allocator);
          }
          if (i.getHeight().has_value()) {
            image.AddMember(
                "height",
                rapidjson::Value(std::to_string(i.getHeight().value()).c_str(),
                                 allocator),
                allocator);
          }
          imagesArray.PushBack(image, allocator);
        }
        screenshot.AddMember("images", imagesArray, allocator);
      }

      if (s.getVideo().has_value()) {
        rapidjson::Value video(rapidjson::kObjectType);
        if (s.getVideo().value().getUrl().has_value()) {
          video.AddMember(
              "url",
              rapidjson::Value(s.getVideo().value().getUrl().value().c_str(),
                               allocator),
              allocator);
        }
        if (s.getVideo().value().getWidth().has_value()) {
          video.AddMember(
              "width",
              rapidjson::Value(
                  std::to_string(s.getVideo().value().getWidth().value()),
                  allocator),
              allocator);
        }
        if (s.getVideo().value().getHeight().has_value()) {
          video.AddMember(
              "height",
              rapidjson::Value(
                  std::to_string(s.getVideo().value().getHeight().value()),
                  allocator),
              allocator);
        }
        if (s.getVideo().value().getCodec().has_value()) {
          video.AddMember(
              "codec",
              rapidjson::Value(s.getVideo().value().getCodec().value(),
                               allocator),
              allocator);
        }
        if (s.getVideo().value().getContainer().has_value()) {
          video.AddMember(
              "container",
              rapidjson::Value(s.getVideo().value().getContainer().value(),
                               allocator),
              allocator);
        }
        screenshot.AddMember("video", video, allocator);
      }
      screenshotsArray.PushBack(screenshot, allocator);
    }
    document.AddMember("screenshots", screenshotsArray, allocator);
  }

  // icons
  if (component.getIcons().has_value()) {
    rapidjson::Value iconsArray(rapidjson::kArrayType);
    for (const auto& i : component.getIcons().value()) {
      rapidjson::Value icon(rapidjson::kObjectType);
      if (i.getType().has_value()) {
        icon.AddMember("type",
                       rapidjson::Value(i.getType().value().c_str(), allocator),
                       allocator);
      }
      if (i.getPath().has_value()) {
        icon.AddMember("path",
                       rapidjson::Value(i.getPath().value().c_str(), allocator),
                       allocator);
      }
      if (i.getScale().has_value()) {
        icon.AddMember(
            "scale",
            rapidjson::Value(std::to_string(i.getScale().value()), allocator),
            allocator);
      }
      if (i.getWidth().has_value()) {
        icon.AddMember(
            "width",
            rapidjson::Value(std::to_string(i.getWidth().value()), allocator),
            allocator);
      }
      if (i.getHeight().has_value()) {
        icon.AddMember(
            "height",
            rapidjson::Value(std::to_string(i.getHeight().value()), allocator),
            allocator);
      }
      iconsArray.PushBack(icon, allocator);
    }
    document.AddMember("icons", iconsArray, allocator);
  }
  // releases
  if (component.getReleases().has_value()) {
    rapidjson::Value releasesArray(rapidjson::kArrayType);
    for (const auto& r : component.getReleases().value()) {
      rapidjson::Value release(rapidjson::kObjectType);
      release.AddMember("version",
                        rapidjson::Value(r.getVersion().c_str(), allocator),
                        allocator);
      release.AddMember("timestamp",
                        rapidjson::Value(r.getTimestamp().c_str(), allocator),
                        allocator);
      if (r.getDescription().has_value()) {
        release.AddMember(
            "description",
            rapidjson::Value(r.getDescription().value().c_str(), allocator),
            allocator);
      }
      if (r.getSize().has_value()) {
        release.AddMember("size",
                          rapidjson::Value(r.getSize().value(), allocator),
                          allocator);
      }
      releasesArray.PushBack(release, allocator);
    }
    document.AddMember("releases", releasesArray, allocator);
  }

  // agreements
  if (component.getAgreement().has_value()) {
    document.AddMember(
        "agreement",
        rapidjson::Value(component.getAgreement().value().c_str(), allocator),
        allocator);
  }

  // projectgroup
  if (component.getProjectGroup().has_value()) {
    document.AddMember(
        "projectgroup",
        rapidjson::Value(component.getProjectGroup().value().c_str(),
                         allocator),
        allocator);
  }

  // suggests
  if (component.getSuggests().has_value()) {
    rapidjson::Value suggestsArray(rapidjson::kArrayType);
    for (const auto& s : component.getSuggests().value()) {
      suggestsArray.PushBack(rapidjson::Value(s.c_str(), allocator), allocator);
    }
    document.AddMember("suggests", suggestsArray, allocator);
  }
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);

  return buffer.GetString();
}

void FlatpakShim::create_sandbox(FlatpakInstalledRef* installed_ref,
                                 const asio::io_context::strand& strand,
                                 std::function<void(bool)> callback) {
  const std::string metadata = get_metadata_as_string(installed_ref);
  const FlatpakShim::sandbox sandbox = parse_metadata(metadata);
  spdlog::debug("[FlatpakPlugin] Create sandbox for: {}",
                sandbox.application.name);

  if (!portal_) {
    portal_ = std::make_unique<Portals>(strand.context());
    portal_->initialize_portals(
        const_cast<asio::io_context::strand&>(strand),
        [sandbox, strand,
         callback = std::move(callback)](bool success) mutable {
          if (!success) {
            spdlog::error("[FlatpakPlugin] Portal not ready");
            callback(false);
            return;
          }
          std::vector<std::string> permissions;
          for (const auto& dev : sandbox.context.devices) {
            if (dev == "dri") {
              permissions.emplace_back("GPU Access");
            }
            if (dev == "all") {
              permissions.emplace_back("All Devices");
            }
          }

          for (const auto& filesystem : sandbox.context.filesystems) {
            if (filesystem.find("xdg-") == 0) {
              permissions.emplace_back("Access " + filesystem.substr(4) +
                                       " folder");
            }
          }

          if (std::find(sandbox.context.sockets.begin(),
                        sandbox.context.sockets.end(),
                        "pulseaudio") != sandbox.context.sockets.end()) {
            permissions.emplace_back("Audio Access");
          }

          portal_->check_permissions(
              const_cast<asio::io_context::strand&>(strand),
              sandbox.application.name, permissions,
              [sandbox, strand, permissions,
               callback = std::move(callback)](bool already_granted) mutable {
                if (already_granted) {
                  spdlog::info(
                      "[FlatpakPlugin] Permissions already granted for app",
                      sandbox.application.name);
                  setup_portal_sessions(
                      sandbox, const_cast<asio::io_context::strand&>(strand),
                      std::move(callback));
                  return;
                }
                bool granted = portal_->create_dialog(sandbox.application.name,
                                                      permissions);
                portal_->store_permissions(
                    const_cast<asio::io_context::strand&>(strand),
                    sandbox.application.name, permissions, granted,
                    [sandbox, granted, strand,
                     callback = std::move(callback)](bool success) mutable {
                      if (!granted) {
                        spdlog::error(
                            "[FlatpakPlugin] Portal not granted for : {}",
                            sandbox.application.name);
                        callback(false);
                        return;
                      }
                      setup_portal_sessions(
                          sandbox,
                          const_cast<asio::io_context::strand&>(strand),
                          std::move(callback));
                    });
              });
        });
  } else {
    setup_portal_sessions(sandbox,
                          const_cast<asio::io_context::strand&>(strand),
                          std::move(callback));
  }
}

void FlatpakShim::setup_portal_sessions(const sandbox& configs,
                                        asio::io_context::strand& strand,
                                        std::function<void(bool)> callback) {
  if (!configs.session_bus.empty()) {
    portal_->setup_portals(
        const_cast<asio::io_context::strand&>(strand), configs.session_bus,
        [configs, callback = std::move(callback)](bool success) mutable {
          g_setenv("XDG_DESKTOP_PORTAL_PATH", "/org/freedesktop/portal/desktop",
                   TRUE);
          g_setenv("FLATPAK_ID", configs.application.name.c_str(), TRUE);
          for (const auto& [fst, snd] : configs.environment) {
            g_setenv(fst.c_str(), snd.c_str(), true);
          }
          callback(true);
        });
  } else {
    g_setenv("FLATPAK_ID", configs.application.name.c_str(), TRUE);
    callback(true);
  }
}

FlatpakShim::sandbox FlatpakShim::parse_metadata(const std::string& metadata) {
  FlatpakShim::sandbox sandbox;
  auto application = extract_metadataSections(metadata, "Application");
  auto context = extract_metadataSections(metadata, "Context");
  const auto session_bus =
      extract_metadataSections(metadata, "Session Bus Policy");
  const auto system_bus =
      extract_metadataSections(metadata, "System Bus Policy");
  auto extra_data = extract_metadataSections(metadata, "Extra Data");
  const auto build = extract_metadataSections(metadata, "Build");
  sandbox.environment = extract_metadataSections(metadata, "Environment");

  // parse application data
  if (application.count("name")) {
    sandbox.application.name = application["name"];
  }
  if (application.count("runtime")) {
    sandbox.application.runtime = application["runtime"];
  }
  if (application.count("sdk")) {
    sandbox.application.sdk = application["sdk"];
  }
  if (application.count("base")) {
    sandbox.application.base = application["base"];
  }
  if (application.count("command")) {
    sandbox.application.command = application["command"];
  }

  // parse context
  if (context.count("shared")) {
    sandbox.context.shared =
        plugin_common::StringTools::split(context["shared"], ";");
  }
  if (context.count("sockets")) {
    sandbox.context.sockets =
        plugin_common::StringTools::split(context["sockets"], ";");
  }
  if (context.count("devices")) {
    sandbox.context.devices =
        plugin_common::StringTools::split(context["devices"], ";");
  }
  if (context.count("filesystems")) {
    sandbox.context.filesystems =
        plugin_common::StringTools::split(context["filesystems"], ";");
  }

  // parse session bus
  for (const auto& [fst, snd] : session_bus) {
    sandbox.session_bus.emplace_back(fst);
  }

  // parse system bus
  for (const auto& [fst, snd] : system_bus) {
    sandbox.system_bus.emplace_back(fst);
  }

  // parse extra data
  if (extra_data.count("name")) {
    sandbox.extra.name = extra_data["name"];
  }
  if (extra_data.count("checksum")) {
    sandbox.extra.checksum = extra_data["checksum"];
  }
  if (extra_data.count("size")) {
    sandbox.extra.size = extra_data["size"];
  }
  if (extra_data.count("uri")) {
    sandbox.extra.uri = extra_data["uri"];
  }

  // parse build
  if (build.count("built-extensions")) {
    sandbox.built_extensions =
        plugin_common::StringTools::split(context["built-extensions"], ";");
  }
  return sandbox;
}

std::map<std::string, std::string> FlatpakShim::extract_metadataSections(
    const std::string& metadata,
    const std::string& section) {
  std::map<std::string, std::string> values;

  std::string escapedName = std::regex_replace(
      section, std::regex(R"([\.\[\](){}*+?^$|\\])"), R"(\$&)");
  std::string pattern = R"(\[)" + escapedName + R"(\]\s*\n((?:[^[].*\n?)*))";
  std::regex sectionReg(pattern);
  if (std::smatch match; std::regex_search(metadata, match, sectionReg)) {
    std::string content = match[1];
    std::regex kvPattern(R"(^([a-zA-Z][a-zA-Z0-9_.-]*)\s*=\s*([^\n]+))");

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
      plugin_common::StringTools::trimSpaces(line);

      if (!line.empty() && line[0] == '[') {
        break;
      }

      if (std::smatch match2; std::regex_match(line, match2, kvPattern)) {
        std::string val = match2[2];
        plugin_common::StringTools::trimSpaces(val);
        values[match2[1]] = val;
      }
    }
  }
  return values;
}

}  // namespace flatpak_plugin