cmake_minimum_required(VERSION 3.16)
project(MultiCanvasLiveEngine VERSION 0.1.0)

# Ensure C++17 is used
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OBS (Assuming OBS is installed and environment variable OBS_ROOT is set)
find_package(OBS REQUIRED)

add_library(multi-canvas-engine MODULE
    src/plugin.cpp
)

# Link obs-frontend-api and libobs
target_link_libraries(multi-canvas-engine PRIVATE OBS::libobs OBS::obs-frontend-api)

# Set the output directory to match OBS plugin structure
set_target_properties(multi-canvas-engine PROPERTIES 
    PREFIX "" 
    OUTPUT_NAME "multi-canvas-engine"
)

install(TARGETS multi-canvas-engine LIBRARY DESTINATION ${OBS_PLUGIN_DEST})
