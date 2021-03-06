cmake_minimum_required(VERSION 3.15)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(TargetArch)

project(MumblePlugin
	VERSION "1.0.0"
	DESCRIPTION "Minimal Mumble plugin"
)

set(PLUGIN_NAME "video")

# JSON config file
find_package(nlohmann_json 3.2.0 REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavdevice
    libavfilter
    libavformat
    libavcodec
    libswresample
    libswscale
    libavutil
)

add_library(plugin
	SHARED
		plugin.cpp
		Streamer.cpp
		Receiver.cpp
		StreamHandler.cpp
)

target_include_directories(plugin
	PUBLIC "${CMAKE_SOURCE_DIR}/include/"
)

# JSON config file
target_link_libraries(
	plugin PRIVATE nlohmann_json::nlohmann_json
	PkgConfig::LIBAV
)

# Add suffix for the respective OS
if (WIN32)
	set(PLUGIN_NAME "${PLUGIN_NAME}_win")
elseif (APPLE)
	set(PLUGIN_NAME "${PLUGIN_NAME}_macos")
elseif(UNIX)
	set(PLUGIN_NAME "${PLUGIN_NAME}_linux")
endif()

# Add suffix for target architecture
target_architecture(TARGET_ARCH)
string(TOLOWER "${TARGET_ARCH}" TARGET_ARCH)
if (NOT TARGET_ARCH STREQUAL "unknown")
	set(PLUGIN_NAME "${PLUGIN_NAME}_${TARGET_ARCH}")
endif()

set_target_properties(plugin PROPERTIES LIBRARY_OUTPUT_NAME "${PLUGIN_NAME}")
