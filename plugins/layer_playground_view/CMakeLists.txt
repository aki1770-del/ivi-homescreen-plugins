include_guard()

# The plugin renders into an FBO using the engine's GL context — no
# per-plugin EGL context, no Wayland subsurface. The compositor composites
# the resulting texture onto the scene at the layer rect.

add_library(plugin_layer_playground_view STATIC
        layer_playground_view_plugin.cc
        layer_playground_view_plugin_c_api.cc
        # Optional Vulkan render path: reuses the wayland-vulkan backend's device
        # (via Backend::GetVulkanContext) to write the gradient into a VkImage
        # when there is no engine GL context. Uses vulkan.hpp + the process-
        # shared dynamic dispatcher owned by the shell — no libvulkan link.
        layer_playground_vulkan.cc
        # ihs_pv (push-model) producer: registers a PlatformViewRegistry factory
        # that negotiates a surface path and submits a solid gbm dma-buf. The
        # factory supersedes the legacy ICompositorSurface path above.
        layer_playground_ihs_pv.cc
)

target_include_directories(plugin_layer_playground_view PUBLIC
        .
        include
        # ivi-homescreen's vendored Vulkan-Headers, matching the version the
        # backend initializes the shared dispatcher against.
        ${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers/include
        # ihs_pv ABI (ihs/platform_view.h) + its generated ihs_export.h.
        ${CMAKE_SOURCE_DIR}/shared/include
        ${CMAKE_BINARY_DIR}/shared/include
)

target_link_libraries(plugin_layer_playground_view PUBLIC
        flutter
        platform_homescreen
        GLESv2
        # The ihs_pv producer allocates scanout-capable buffers via gbm.
        gbm
)
