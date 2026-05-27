# Phase 0 stub — dispmanx renderer, implemented in Phase 3+
# Requires DISPMANX sysroot from Pi (/opt/vc)
add_library(dispmanx_renderer STATIC renderer_stub.c)
target_include_directories(dispmanx_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
