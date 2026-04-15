include_guard()

# The plugin renders into an FBO using the engine's GL context — no
# per-plugin EGL context, no Wayland subsurface. The compositor composites
# the resulting texture onto the scene at the layer rect.

add_library(plugin_layer_playground_view STATIC
        layer_playground_view_plugin.cc
        layer_playground_view_plugin_c_api.cc
)

target_include_directories(plugin_layer_playground_view PUBLIC
        .
        include
)

target_link_libraries(plugin_layer_playground_view PUBLIC
        flutter
        platform_homescreen
        GLESv2
)
