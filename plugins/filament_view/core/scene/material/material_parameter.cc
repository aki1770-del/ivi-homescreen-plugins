/*
 * Copyright 2020-2024 Toyota Connected North America
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

#include "material_parameter.h"

#include <core/utils/asserts.h>

#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <plugins/common/common.h>

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
MaterialParameter::MaterialParameter(
  std::string name,
  const MaterialType type,
  MaterialTextureValue value
)
  : name_(std::move(name)),
    type_(type),
    textureValue_(std::move(value)) {}

////////////////////////////////////////////////////////////////////////////
MaterialParameter::MaterialParameter(
  std::string name,
  const MaterialType type,
  MaterialFloatValue value
)
  : name_(std::move(name)),
    type_(type),
    fValue_(value) {}

MaterialParameter::MaterialParameter(
  std::string name,
  const MaterialType type,
  MaterialIntValue value
)
  : name_(std::move(name)),
    type_(type),
    iValue_(value) {}

MaterialParameter::MaterialParameter(
  std::string name,
  const MaterialType type,
  MaterialColorValue value
)
  : name_(std::move(name)),
    type_(type),
    fVecValue_(MaterialFloatVectorValue(4, 0.0f)) {
  memcpy(fVecValue_.value().data(), &value, sizeof(float) * 4);
}

MaterialParameter::MaterialParameter(
  std::string name,
  const MaterialType type,
  MaterialFloatVectorValue value
)
  : name_(std::move(name)),
    type_(type),
    fVecValue_(std::move(value)) {}

////////////////////////////////////////////////////////////////////////////
std::unique_ptr<MaterialParameter> MaterialParameter::Deserialize(
  const std::string& /* flutter_assets_path */,
  const flutter::EncodableMap& params
) {
  SPDLOG_TRACE("++{}", __FUNCTION__);

  std::optional<std::string> name;
  std::optional<MaterialType> type;

  std::optional<flutter::EncodableMap> encodMapValue;
  std::optional<flutter::EncodableList> encodListValue;

  std::unique_ptr<MaterialParameter> retval = nullptr;

  for (const auto& [fst, snd] : params) {
    auto key = std::get<std::string>(fst);
    if (snd.IsNull()) {
      SPDLOG_DEBUG("MaterialParameter Param Second mapping is null {} {}", key, __FUNCTION__);
      continue;
    }

    const _MaterialField field = GetFieldForText(key);

    switch (field) {
      case _MaterialField::NAME:
        runtime_assert(std::holds_alternative<std::string>(snd), "name must be a string");
        name = std::get<std::string>(snd);
        break;
      case _MaterialField::TYPE:
        runtime_assert(name.has_value(), "name is missing or set after type");
        runtime_assert(
          std::holds_alternative<std::string>(snd),
          fmt::format("type must be a string (key: {})", key)
        );
        type = getTypeForText(std::get<std::string>(snd));
        break;

      case _MaterialField::VALUE:
        runtime_assert(
          type.has_value(),
          fmt::format("type is missing or set after value (name: {})", name.value())
        );
        switch (type.value()) {
          case MaterialType::FLOAT:
            runtime_assert(std::holds_alternative<double>(snd), "value must be a double");
            retval = std::make_unique<MaterialParameter>(
              name.value(), type.value(), static_cast<float>(std::get<double>(snd))
            );
            break;
          case MaterialType::INT:
            runtime_assert(std::holds_alternative<int>(snd), "value must be an int");
            retval = std::make_unique<MaterialParameter>(
              name.value(), type.value(), std::get<int>(snd)
            );
            break;
          case MaterialType::FLOAT2:
            [[fallthrough]];
          case MaterialType::FLOAT3:
            [[fallthrough]];
          case MaterialType::COLOR:
            [[fallthrough]];
          case MaterialType::FLOAT4: {
            const size_t size = GetVectorSizeForType(type.value());

            runtime_assert(
              std::holds_alternative<flutter::EncodableList>(snd),
              fmt::format(
                "value must be a EncodableList (name: {}, type: {})",  //
                name.value(), getTextForType(type.value())
              )
            );
            // temporarily store in encodListValue
            encodListValue = std::get<flutter::EncodableList>(snd);
            const auto& list = encodListValue.value();
            runtime_assert(
              list.size() == size, fmt::format("vector value must have {} elements", size)
            );
            for (const auto& item : list) {
              runtime_assert(
                std::holds_alternative<double>(item), "color value elements must be double"
              );
            }

            // create vector with correct length
            MaterialFloatVectorValue fvec(size, 0.0f);
            float* vec = fvec.data();
            for (size_t i = 0; i < size; ++i) {
              vec[i] = static_cast<float>(std::get<double>(list[i]));
            }

            retval = std::make_unique<MaterialParameter>(name.value(), type.value(), fvec);
          } break;
          case MaterialType::TEXTURE:
            runtime_assert(
              std::holds_alternative<flutter::EncodableMap>(snd), "value must be a map"
            );
            encodMapValue = std::get<flutter::EncodableMap>(snd);
            retval = std::make_unique<MaterialParameter>(
              name.value(), type.value(), TextureDefinitions::Deserialize(encodMapValue.value())
            );
            break;
          case MaterialType::BOOL:
            runtime_assert(std::holds_alternative<bool>(snd), "value must be a bool");
            retval = std::make_unique<MaterialParameter>(
              name.value(), type.value(), std::get<bool>(snd)
            );
            break;
          default:
            throw std::runtime_error(fmt::format(
              "[MaterialParameter::Deserialize] Unhandled Parameter value type {}",
              getTextForType(type.value())
            ));
        }
        break;
      case _MaterialField::UNKNOWN:
        [[fallthrough]];
      default:
        SPDLOG_WARN("[MaterialParameter::Deserialize] unknown key {} {}", key, __FUNCTION__);
        break;
    }
  }

  runtime_assert(retval != nullptr, "Failed to deserialize MaterialParameter");
  return retval;
}

////////////////////////////////////////////////////////////////////////////
MaterialParameter::~MaterialParameter() = default;

////////////////////////////////////////////////////////////////////////////
void MaterialParameter::debugPrint(const char* tag) {
  spdlog::debug("{}name: '{}', type: '{}',  ", tag, name_, getTextForType(type_));

  // Print value based on type
  switch (type_) {
    case MaterialType::COLOR:
      spdlog::debug(
        "{}value: [{:.3}, {:.3}, {:.3}, {:.3}]", tag,  //
        fVecValue_.value()[0], fVecValue_.value()[1], fVecValue_.value()[2], fVecValue_.value()[3]
      );
      break;
    case MaterialType::FLOAT2:
      spdlog::debug(
        "{}value: [{:.3}, {:.3}]", tag,  //
        fVecValue_.value()[0], fVecValue_.value()[1]
      );
      break;
    case MaterialType::FLOAT3:
      spdlog::debug(
        "{}value: [{:.3}, {:.3}, {:.3}]", tag,  //
        fVecValue_.value()[0], fVecValue_.value()[1], fVecValue_.value()[2]
      );
      break;
    case MaterialType::FLOAT4:
      spdlog::debug(
        "{}value: [{:.3}, {:.3}, {:.3}, {:.3}]", tag,  //
        fVecValue_.value()[0], fVecValue_.value()[1], fVecValue_.value()[2], fVecValue_.value()[3]
      );
      break;
    case MaterialType::FLOAT:
      spdlog::debug(
        "{}value: {:.3}", tag,  //
        fValue_.value()
      );
      break;
    case MaterialType::INT:
      spdlog::debug(
        "{}value: {}", tag,  //
        iValue_.value()
      );
      break;
    case MaterialType::TEXTURE: {
      if (textureValue_.has_value()) {
        const auto texture =
          std::get<std::unique_ptr<TextureDefinitions>>(textureValue_.value()).get();
        if (texture) {
          texture->debugPrint(std::string(tag + std::string("  texture:")).c_str());
        }
      } else {
        spdlog::debug("{}value: <no texture>", tag);
      }
    } break;
    default:
      spdlog::debug("{}value: <unhandled type>", tag);
      break;
  }
}

////////////////////////////////////////////////////////////////////////////
const char* MaterialParameter::getTextForType(MaterialType type) {
  return (const char*[]){
    "COLOR",  "BOOL", "BOOL_VECTOR", "FLOAT", "FLOAT2", "FLOAT3",
    "FLOAT4", "INT",  "INT_VECTOR",  "MAT3",  "MAT4",   "TEXTURE",
  }[static_cast<int>(type)];
}

////////////////////////////////////////////////////////////////////////////
MaterialParameter::MaterialType MaterialParameter::getTypeForText(const std::string& type) {
  // TODO Change to map for faster lookup
  if (type == "COLOR") {
    return MaterialType::COLOR;
  }
  if (type == "BOOL") {
    return MaterialType::BOOL;
  }
  if (type == "BOOL_VECTOR") {
    return MaterialType::BOOL_VECTOR;
  }
  if (type == "FLOAT") {
    return MaterialType::FLOAT;
  }
  if (type == "FLOAT2") {
    return MaterialType::FLOAT2;
  }
  if (type == "FLOAT3") {
    return MaterialType::FLOAT3;
  }
  if (type == "FLOAT4") {
    return MaterialType::FLOAT4;
  }
  if (type == "INT") {
    return MaterialType::INT;
  }
  if (type == "INT_VECTOR") {
    return MaterialType::INT_VECTOR;
  }
  if (type == "MAT3") {
    return MaterialType::MAT3;
  }
  if (type == "MAT4") {
    return MaterialType::MAT4;
  }
  if (type == "TEXTURE") {
    return MaterialType::TEXTURE;
  }
  return MaterialType::INT;
}

////////////////////////////////////////////////////////////////////////////
MaterialColorValue MaterialParameter::HexToColorFloat4(const std::string& hex) {
  // Ensure the string starts with '#' and is the correct length
  if (hex[0] != '#' || hex.length() != 9) {
    throw std::invalid_argument("Invalid hex color format: " + hex);
  }

  // Comes across from our dart as ARGB

  // Extract the hex values for each channel
  unsigned int r, g, b, a;
  std::stringstream ss;
  ss << std::hex << hex.substr(1, 2);
  ss >> a;
  ss.clear();
  ss << std::hex << hex.substr(3, 2);
  ss >> r;
  ss.clear();
  ss << std::hex << hex.substr(5, 2);
  ss >> g;
  ss.clear();
  ss << std::hex << hex.substr(7, 2);
  ss >> b;

  // Convert to float in the range [0, 1]
  MaterialColorValue color;
  color.r = static_cast<float>(r) / 255.0f;
  color.g = static_cast<float>(g) / 255.0f;
  color.b = static_cast<float>(b) / 255.0f;
  color.a = static_cast<float>(a) / 255.0f;

  return color;
}

}  // namespace plugin_filament_view
