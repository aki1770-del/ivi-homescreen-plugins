/*
 * Copyright 2023-2024 Toyota Connected North America
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

#include "encodable.h"

#include "../logging.h"

namespace plugin_common::Encodable {

void PrintFlutterEncodableMap(const char* name,  // NOLINT(misc-no-recursion)
                              const flutter::EncodableMap& args) {
  ihs::log::warn("[{}]", name);
  for (const auto& [fst, snd] : args) {
    auto key = std::get<std::string>(fst);
    PrintFlutterEncodableValue(key.c_str(), snd);
  }
}

void PrintFlutterEncodableList(const char* name,  // NOLINT(misc-no-recursion)
                               const flutter::EncodableList& list) {
  ihs::log::warn("[EncodableList]");
  for (auto& it : list) {
    PrintFlutterEncodableValue(name, it);
  }
}

void PrintFlutterEncodableValue(const char* key,  // NOLINT(misc-no-recursion)
                                const flutter::EncodableValue& it) {
  if (std::holds_alternative<std::monostate>(it)) {
    ihs::log::warn("\t{}: []", key);
  } else if (std::holds_alternative<bool>(it)) {
    auto value = std::get<bool>(it);
    ihs::log::warn("\t{}: bool: {}", key, value);
  } else if (std::holds_alternative<int32_t>(it)) {
    auto value = std::get<int32_t>(it);
    ihs::log::warn("\t{}: int32_t: {}", key, value);
  } else if (std::holds_alternative<int64_t>(it)) {
    auto value = std::get<int64_t>(it);
    ihs::log::warn("\t{}: int64_t: {}", key, value);
  } else if (std::holds_alternative<double>(it)) {
    auto value = std::get<double>(it);
    ihs::log::warn("\t{}: double: {}", key, value);
  } else if (std::holds_alternative<std::string>(it)) {
    auto value = std::get<std::string>(it);
    ihs::log::warn("\t{}: std::string: [{}]", key, value);
  } else if (std::holds_alternative<std::vector<uint8_t>>(it)) {
    const auto value = std::get<std::vector<uint8_t>>(it);
    ihs::log::warn("\t{}: std::vector<uint8_t>", key);
    for (auto const& v : value) {
      ihs::log::warn("\t\t{}", v);
    }
  } else if (std::holds_alternative<std::vector<int32_t>>(it)) {
    const auto value = std::get<std::vector<int32_t>>(it);
    ihs::log::warn("\t{}: std::vector<int32_t>", key);
    for (auto const& v : value) {
      ihs::log::warn("\t\t{}", v);
    }
  } else if (std::holds_alternative<std::vector<int64_t>>(it)) {
    const auto value = std::get<std::vector<int64_t>>(it);
    ihs::log::warn("\t{}: std::vector<int64_t>", key);
    for (auto const& v : value) {
      ihs::log::warn("\t\t{}", v);
    }
  } else if (std::holds_alternative<std::vector<float>>(it)) {
    const auto value = std::get<std::vector<float>>(it);
    ihs::log::warn("\t{}: std::vector<float>", key);
    for (auto const& v : value) {
      ihs::log::warn("\t\t{}", v);
    }
  } else if (std::holds_alternative<std::vector<double>>(it)) {
    const auto value = std::get<std::vector<double>>(it);
    ihs::log::warn("\t{}: std::vector<double>", key);
    for (auto const& v : value) {
      ihs::log::warn("\t\t{}", v);
    }
  } else if (std::holds_alternative<flutter::EncodableList>(it)) {
    ihs::log::warn("\t{}: flutter::EncodableList", key);
    const auto val = std::get<flutter::EncodableList>(it);
    PrintFlutterEncodableList(key, val);
  } else if (std::holds_alternative<flutter::EncodableMap>(it)) {
    ihs::log::warn("\t{}: flutter::EncodableMap", key);
    const auto val = std::get<flutter::EncodableMap>(it);
    PrintFlutterEncodableMap(key, val);
  } else {
    ihs::log::error("\t{}: unknown type", key);
    assert(false);
  }
}
}  // namespace plugin_common::Encodable
