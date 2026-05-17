cmake_minimum_required(VERSION 3.24)

project(obs-dual-vertical-stream VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(OBS_STUDIO_DIR "" CACHE PATH "Path to OBS Studio source/build install root")
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
	set(QT_WIDGETS_TARGET Qt6::Widgets)
else()
	find_package(Qt5 COMPONENTS Widgets REQUIRED)
	set(QT_WIDGETS_TARGET Qt5::Widgets)
endif()

find_package(libobs QUIET)
find_package(obs-frontend-api QUIET)

add_library(obs-dual-vertical-stream MODULE
	plugin.cpp
)

target_compile_definitions(obs-dual-vertical-stream PRIVATE
	UNICODE
	_UNICODE
)

target_link_libraries(obs-dual-vertical-stream PRIVATE
	${QT_WIDGETS_TARGET}
)

if(TARGET OBS::libobs)
	target_link_libraries(obs-dual-vertical-stream PRIVATE OBS::libobs)
elseif(TARGET libobs)
	target_link_libraries(obs-dual-vertical-stream PRIVATE libobs)
elseif(OBS_STUDIO_DIR)
	target_include_directories(obs-dual-vertical-stream PRIVATE
		"${OBS_STUDIO_DIR}/libobs"
		"${OBS_STUDIO_DIR}/UI/obs-frontend-api"
	)
	target_link_directories(obs-dual-vertical-stream PRIVATE
		"${OBS_STUDIO_DIR}/build/libobs/RelWithDebInfo"
		"${OBS_STUDIO_DIR}/build/UI/obs-frontend-api/RelWithDebInfo"
		"${OBS_STUDIO_DIR}/build/libobs/Release"
		"${OBS_STUDIO_DIR}/build/UI/obs-frontend-api/Release"
	)
	target_link_libraries(obs-dual-vertical-stream PRIVATE
		libobs
		obs-frontend-api
	)
else()
	message(FATAL_ERROR "Could not find libobs. Set CMAKE_PREFIX_PATH or OBS_STUDIO_DIR.")
endif()

if(TARGET OBS::frontend-api)
	target_link_libraries(obs-dual-vertical-stream PRIVATE OBS::frontend-api)
elseif(TARGET obs-frontend-api)
	target_link_libraries(obs-dual-vertical-stream PRIVATE obs-frontend-api)
endif()

set_target_properties(obs-dual-vertical-stream PROPERTIES
	PREFIX ""
	OUTPUT_NAME "obs-dual-vertical-stream"
)

if(WIN32)
	set_target_properties(obs-dual-vertical-stream PROPERTIES
		SUFFIX ".dll"
	)
endif()