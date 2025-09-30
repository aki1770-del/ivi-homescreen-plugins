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

#pragma once

#include "texture/texture_definitions.h"
#include "texture/texture_sampler.h"

#include <math/vec4.h>
#include <memory>

#include <flutter/encodable_value.h>

#define _MATPARAM_VEC_MAX_SIZE 4

namespace plugin_filament_view {

class TextureDefinitions;

class TextureSampler;

using MaterialTextureValue = std::variant<std::unique_ptr<TextureDefinitions>>;
using MaterialFloatValue = float;
using MaterialIntValue = int32_t;
using MaterialBoolValue = bool;
using MaterialColorValue = ::filament::math::vec4<float>;
using MaterialFloatVectorValue = std::vector<float>;

class MaterialParameter {
  private:
    enum class _MaterialField { NAME, TYPE, VALUE, UNKNOWN };

  public:
    enum class MaterialType {
      // color can be presented by int or Color like Colors.white
      COLOR,
      BOOL,
      BOOL_VECTOR,
      FLOAT,
      FLOAT2,
      FLOAT3,
      FLOAT4,
      INT,
      INT_VECTOR,
      MAT3,
      MAT4,
      TEXTURE,
    };

    MaterialParameter(std::string name, const MaterialType type, MaterialTextureValue value);
    MaterialParameter(std::string name, const MaterialType type, MaterialFloatValue value);
    MaterialParameter(std::string name, const MaterialType type, MaterialIntValue value);
    MaterialParameter(std::string name, const MaterialType type, MaterialColorValue value);
    MaterialParameter(std::string name, const MaterialType type, MaterialFloatVectorValue value);

    static std::unique_ptr<MaterialParameter> Deserialize(
      const std::string& flutter_assets_path,
      const flutter::EncodableMap& params
    );

    ~MaterialParameter();

    void debugPrint(const char* tag);

    [[nodiscard]] std::string szGetParameterName() const { return name_; }

    friend class Material;
    friend class MaterialDefinitions;

    [[nodiscard]] const MaterialTextureValue& getTextureValue() const {
      if (textureValue_.has_value()) {
        return textureValue_.value();
      } else {
        throw std::runtime_error("MaterialParameter does not contain a texture value.");
      }
    }

    [[nodiscard]] TextureSampler* getTextureSampler() const {
      const auto& textureValue = getTextureValue();
      const auto& texturePtr = std::get<std::unique_ptr<TextureDefinitions>>(textureValue);

      if (!texturePtr) {
        return nullptr;
      }

      return texturePtr->getSampler();
    }

    [[nodiscard]] std::string getTextureValueAssetPath() const {
      const auto& textureValue = getTextureValue();
      const auto& texturePtr = std::get<std::unique_ptr<TextureDefinitions>>(textureValue);

      if (!texturePtr) {
        return "";
      }

      return texturePtr->szGetTextureDefinitionLookupName();
    }

    [[nodiscard]] std::unique_ptr<MaterialParameter> clone() const {
      switch (type_) {
        case MaterialType::FLOAT2:
          [[fallthrough]];
        case MaterialType::FLOAT3:
          [[fallthrough]];
        case MaterialType::FLOAT4:
          [[fallthrough]];
        case MaterialType::COLOR:
          return std::make_unique<MaterialParameter>(name_, type_, fVecValue_.value());
        case MaterialType::FLOAT:
          return std::make_unique<MaterialParameter>(name_, type_, fValue_.value());
        case MaterialType::TEXTURE:
          if (textureValue_.has_value()) {
            const auto& textureValue = textureValue_.value();
            const auto& texturePtr = std::get<std::unique_ptr<TextureDefinitions>>(textureValue);
            if (texturePtr) {
              // Assuming TextureDefinitions has a clone method
              return std::make_unique<MaterialParameter>(name_, type_, texturePtr->clone());
            }
          }
          break;
        case MaterialType::INT:
          break;
        // Handle other types (BOOL, BOOL_VECTOR, INT, etc.)
        case MaterialType::BOOL:
          // TODO: Add cloning logic for these types
        default:
          throw std::runtime_error("Unsupported MaterialType in clone.");
      }
      return nullptr;  // In case of unsupported type or missing value.
    }

  private:
    std::string name_;
    MaterialType type_;
    std::optional<MaterialTextureValue> textureValue_;
    std::optional<MaterialFloatValue> fValue_;
    std::optional<MaterialFloatVectorValue> fVecValue_;
    std::optional<MaterialIntValue> iValue_;
    std::optional<MaterialBoolValue> bValue_;

    // TODO delete this, colorOf functionality exists in base filament.
    static MaterialColorValue HexToColorFloat4(const std::string& hex);

    static const char* getTextForType(MaterialType type);

    static MaterialType getTypeForText(const std::string& type);

    static _MaterialField GetFieldForText(const std::string& field) {
      if (field == "name") {
        return _MaterialField::NAME;
      }
      if (field == "type") {
        return _MaterialField::TYPE;
      }
      if (field == "value") {
        return _MaterialField::VALUE;
      }
      return _MaterialField::UNKNOWN;
    }

    static size_t GetVectorSizeForType(MaterialType type) {
      switch (type) {
        case MaterialType::COLOR:
          [[fallthrough]];
        case MaterialType::FLOAT4:
          return 4;
        case MaterialType::FLOAT3:
          return 3;
        case MaterialType::FLOAT2:
          return 2;
        default:
          return 1;
      }
    }
};
}  // namespace plugin_filament_view
