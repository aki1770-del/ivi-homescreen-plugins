/*
 * Copyright 2024-2025 Toyota Connected North America
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

#ifndef MESSAGES_H_
#define MESSAGES_H_
#include <flutter/basic_message_channel.h>
#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/shell/platform/common/json_method_codec.h>
#include <flutter/standard_method_codec.h>

#include <map>
#include <string>
#include <utility>

namespace plugin_xdg_icons {

class FlutterError {
 public:
  explicit FlutterError(std::string code) : code_(std::move(code)) {}
  explicit FlutterError(std::string code, std::string message)
      : code_(std::move(code)), message_(std::move(message)) {}
  explicit FlutterError(std::string code,
                        std::string message,
                        flutter::EncodableValue details)
      : code_(std::move(code)),
        message_(std::move(message)),
        details_(std::move(details)) {}

  [[nodiscard]] const std::string& code() const { return code_; }
  [[nodiscard]] const std::string& message() const { return message_; }
  [[nodiscard]] const flutter::EncodableValue& details() const {
    return details_;
  }

 private:
  std::string code_;
  std::string message_;
  flutter::EncodableValue details_;
};

template <class T>
class ErrorOr {
 public:
  ErrorOr(const T& rhs) : v_(rhs) {}
  ErrorOr(const T&& rhs) : v_(std::move(rhs)) {}
  ErrorOr(const FlutterError& rhs) : v_(rhs) {}
  ErrorOr(const FlutterError&& rhs) : v_(std::move(rhs)) {}

  [[nodiscard]] bool has_error() const {
    return std::holds_alternative<FlutterError>(v_);
  }
  const T& value() const { return std::get<T>(v_); };
  [[nodiscard]] const FlutterError& error() const {
    return std::get<FlutterError>(v_);
  };

 private:
  friend class IntegrationTestApi;
  ErrorOr() = default;
  T TakeValue() && { return std::get<T>(std::move(v_)); }

  std::variant<T, FlutterError> v_;
};

// Generated interface from Pigeon that represents a handler of messages from
// Flutter.
class XdgIconsApi {
 public:
  XdgIconsApi(const XdgIconsApi&) = delete;
  XdgIconsApi& operator=(const XdgIconsApi&) = delete;
  virtual ~XdgIconsApi() = default;

  virtual ErrorOr<flutter::EncodableValue> LookupIcon(
      const flutter::EncodableMap& map) = 0;

  // The codec used by DesktopWindowApi.
  static const flutter::StandardMethodCodec& GetCodec();
  // Sets up an instance of `DesktopWindowApi` to handle messages through the
  // `binary_messenger`.
  static void SetUp(flutter::BinaryMessenger* binary_messenger,
                    XdgIconsApi* api);
  static flutter::EncodableValue WrapError(std::string_view error_message);
  static flutter::EncodableValue WrapError(const FlutterError& error);

 protected:
  XdgIconsApi() = default;
};

}  // namespace plugin_xdg_icons
#endif  // MESSAGES_H_
