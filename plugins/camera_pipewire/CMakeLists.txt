#
# Copyright 2023-2024 Toyota Connected North America
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
find_package(PkgConfig REQUIRED)

pkg_check_modules(PIPEWIRE IMPORTED_TARGET REQUIRED libpipewire-0.3)
pkg_check_modules(JPEG IMPORTED_TARGET REQUIRED libjpeg)

add_library(plugin_camera_pipewire STATIC
        camera_plugin_c_api.cc
        camera_plugin.cc
        messages.g.cc
        CameraStream.cc
        CameraStream.h
        CameraManager.cc
        CameraManager.h
        PipewireGraph.cc
        PipewireGraph.h
)

target_include_directories(plugin_camera_pipewire PUBLIC
        include
)

target_link_libraries(plugin_camera_pipewire PUBLIC
        PkgConfig::PIPEWIRE
        PkgConfig::JPEG
        flutter
        platform_homescreen
        plugin_common
)

# Where CLion runner loads .so files
set(BLOBS_DIR "${CMAKE_BINARY_DIR}/shell/blobs")

# TF Lite build root (adjust if yours differs)
set(TFLITE_ROOT "/home/tcna/dev/tensorflow/tensorflow/lite/build" CACHE PATH "TF Lite build root")

# All Abseil .so's (any subdir)
file(GLOB_RECURSE ABSL_ALL
        "${TFLITE_ROOT}/_deps/abseil-cpp-build/absl/*/libabsl_*.so*")

# XNNPACK & friends (from your ldd -r)
file(GLOB_RECURSE XNNPACK_LIB      "${TFLITE_ROOT}/**/libXNNPACK.so")
file(GLOB_RECURSE PTHREADPOOL_LIB  "${TFLITE_ROOT}/**/libpthreadpool.so")
file(GLOB_RECURSE CPUINFO_LIB      "${TFLITE_ROOT}/**/libcpuinfo.so")
file(GLOB_RECURSE EIGHTBIT_LIB     "${TFLITE_ROOT}/**/libeight_bit_int_gemm.so")
file(GLOB_RECURSE FFT2D_LIBS       "${TFLITE_ROOT}/**/libfft2d_fftsg*.so*")
file(GLOB_RECURSE FARMHASH_LIB     "${TFLITE_ROOT}/**/libfarmhash.so")

add_custom_target(bundle_tflite_deps ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BLOBS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ABSL_ALL}        "${BLOBS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XNNPACK_LIB}     "${BLOBS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PTHREADPOOL_LIB} "${BLOBS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CPUINFO_LIB}     "${BLOBS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EIGHTBIT_LIB}    "${BLOBS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFT2D_LIBS}      "${BLOBS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FARMHASH_LIB}    "${BLOBS_DIR}"
        VERBATIM
)

# ensure it runs; tie it to something CLion always builds
add_dependencies(plugin_camera_pipewire bundle_tflite_deps)