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

#include <filament/gltfio/math.h>
#include <filament/math/mat3.h>
#include <filament/math/mat4.h>
#include <filament/math/quat.h>
#include <filament/math/scalar.h>
#include <filament/math/vec2.h>
#include <filament/math/vec3.h>
#include <filament/math/vec4.h>

#include <cmath>

namespace plugin_filament_view {

/// Float version of M_PI
static constexpr float kPif = static_cast<float>(M_PI);

class VectorUtils {
  public:
    /// Static identity quaternion
    // NOTE: Filament's quaternion constructor takes WXYZ, not XYZW!!!
    static constexpr filament::math::quatf kIdentityQuat =
      filament::math::quatf(1.0f, 0.0f, 0.0f, 0.0f);

    /// Static identity 3x3 matrix
    static constexpr filament::math::mat3f kIdentityMat3f = filament::math::mat3f(
      1.0f,
      0.0f,
      0.0f,  //
      0.0f,
      1.0f,
      0.0f,  //
      0.0f,
      0.0f,
      1.0f  //
    );

    /// Static identity 4x4 matrix
    static constexpr filament::math::mat4f kIdentityMat4f = filament::math::mat4f(
      1.0f,
      0.0f,
      0.0f,
      0.0f,  //
      0.0f,
      1.0f,
      0.0f,
      0.0f,  //
      0.0f,
      0.0f,
      1.0f,
      0.0f,  //
      0.0f,
      0.0f,
      0.0f,
      1.0f  //
    );

    /// Vector3 zero
    static constexpr filament::math::float3 kFloat3Zero = filament::math::float3(
      0.0f,  // x
      0.0f,  // y
      0.0f   // z
    );

    /// Vector3 one
    static constexpr filament::math::float3 kFloat3One = filament::math::float3(
      1.0f,  // x
      1.0f,  // y
      1.0f   // z
    );

    /// Vector3 up direction
    static constexpr filament::math::float3 kUp3 = filament::math::float3(
      0.0f,  // x
      1.0f,  // y
      0.0f   // z
    );

    /// Vector3 down direction
    static constexpr filament::math::float3 kDown3 = filament::math::float3(
      0.0f,   // x
      -1.0f,  // y
      0.0f    // z
    );

    /// Vector3 forward direction
    static constexpr filament::math::float3 kForward3 = filament::math::float3(
      0.0f,  // x
      0.0f,  // y
      -1.0f  // z
    );

    /// Utility function to create an identity 3x3 matrix

    // Utility functions to create identity matrices
    static filament::math::mat3f identity3x3();
    static filament::math::mat4f identity4x4();

    static filament::math::float3 transformPositionVector(
      const filament::math::float3& pos,
      const filament::math::mat4f& transform
    );
    static filament::math::float3 transformScaleVector(
      const filament::math::float3& scale,
      const filament::math::mat4f& transform
    );

    /// Converts Euler angles (in radians) to a quaternion.
    /// @param yaw Yaw angle in radians (Euler angle Y)
    /// @param pitch Pitch angle in radians (Euler angle X)
    /// @param roll Roll angle in radians (Euler angle Z)
    /// @returns Quaternion representing the rotation.
    /// @note up always points to Y+ axis.
    static filament::math::quatf fromEulerAngles(float yaw, float pitch, float roll);

    static filament::math::quatf lookAt(
      const filament::math::float3& position,
      const filament::math::float3& target
    );

    /// Returns the translation component of a transformation matrix.
    static inline filament::math::float3 translationFromMatrix(const filament::math::mat4f& matrix
    ) {
      return filament::math::float3(matrix[3].x, matrix[3].y, matrix[3].z);
    }
};

}  // namespace plugin_filament_view
