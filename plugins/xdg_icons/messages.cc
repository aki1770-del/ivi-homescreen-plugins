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

#undef _HAS_EXCEPTIONS

#include "messages.h"

#include <flutter/basic_message_channel.h>
#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/method_call.h>
#include <flutter/method_channel.h>

#include <string>

#include "plugins/common/common.h"

namespace plugin_xdg_icons {

using flutter::BasicMessageChannel;
using flutter::CustomEncodableValue;
using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::MethodCall;
using flutter::MethodResult;

/// The codec used by IntegrationTestApi.
const flutter::StandardMethodCodec& XdgIconsApi::GetCodec() {
  return flutter::StandardMethodCodec::GetInstance();
}

// Sets up an instance of `IntegrationTest` to handle messages through the
// `binary_messenger`.
void XdgIconsApi::SetUp(flutter::BinaryMessenger* binary_messenger,
                        XdgIconsApi* api) {
  {
    const auto channel = std::make_unique<flutter::MethodChannel<>>(
        binary_messenger, "xdg_icons", &GetCodec());
    if (api != nullptr) {
      channel->SetMethodCallHandler(
          [api](const MethodCall<>& call,
                const std::unique_ptr<MethodResult<>>& result) {
            const auto& method = call.method_name();
            SPDLOG_DEBUG("[xdg_icons] {}", method);

            if (method == "lookupIcon") {
              const auto args = std::get_if<EncodableMap>(call.arguments());
              if (args->empty()) {
                return result->Error("argument_error", "no arguments provided");
              }
              const auto value = api->LookupIcon(*args);
              if (value.has_error()) {
                return result->Error(value.error().code(),
                                     value.error().message(),
                                     value.error().details());
              }
              return result->Success(value.value());
            }
            return result->Error(
                "Could not schedule frame, Not implemented yet");
          });
    } else {
      channel->SetMethodCallHandler(nullptr);
    }
  }
}

}  // namespace plugin_xdg_icons