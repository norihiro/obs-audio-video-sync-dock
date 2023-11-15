cmake_minimum_required(VERSION 3.12)

project(obs-audio-video-sync-dock VERSION 0.2.1)

set(PLUGIN_AUTHOR "Norihiro Kamae")

# Replace `com.example.obs-plugin-template` with a unique Bundle ID for macOS releases
# (used both in the installer and when submitting the installer for notarization)
set(MACOS_BUNDLEID "net.nagater.obs-audio-video-sync-dock")
set(ID_PREFIX "net.nagater.obs-audio-video-sync-dock.")
set(MACOS_PACKAGE_UUID "10FED78A-2D7F-494B-A7B0-780280CB09B8")
set(MACOS_INSTALLER_UUID "C0ADE8B3-B56D-4328-BEAF-0A967FED280B")

# Replace `me@contoso.com` with the maintainer email address you want to put in Linux packages
set(LINUX_MAINTAINER_EMAIL "norihiro@nagater.net")

# TAKE NOTE: No need to edit things past this point

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
include(cmake/ObsPluginHelpers.cmake)
find_qt(VERSION ${QT_VERSION} COMPONENTS Widgets Core Gui)

configure_file(
	src/plugin-macros.h.in
	plugin-macros.generated.h
)
configure_file(
	installer/installer-Windows.iss.in
	../installer/installer-Windows.generated.iss
)

configure_file(
	ci/ci_includes.sh.in
	../ci/ci_includes.generated.sh
)
configure_file(
	ci/ci_includes.cmd.in
	../ci/ci_includes.generated.cmd
)

set(PLUGIN_SOURCES
	src/plugin-main.c
	src/sync-test-output.cpp
	src/sync-test-dock.cpp
	src/dock-compat.cpp
	deps/quirc/lib/decode.c
	deps/quirc/lib/identify.c
	deps/quirc/lib/quirc.c
	deps/quirc/lib/version_db.c
)

add_library(${CMAKE_PROJECT_NAME} MODULE ${PLUGIN_SOURCES})

target_link_libraries(${CMAKE_PROJECT_NAME}
	OBS::libobs
	OBS::obs-frontend-api
	Qt::Core
	Qt::Widgets
	Qt::Gui
	Qt::GuiPrivate
)

option(ENABLE_PROFILE "Enable profile" OFF)
if(ENABLE_PROFILE)
	target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE "-DENABLE_PROFILE")
endif()

target_include_directories(${CMAKE_PROJECT_NAME}
	PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/deps/quirc/lib
)

if(OS_WINDOWS)
	# Enable Multicore Builds and disable FH4 (to not depend on VCRUNTIME140_1.DLL when building with VS2019)
	if (MSVC)
		add_definitions(/MP /d2FH4-)
		add_definitions("-D_USE_MATH_DEFINES") # for M_PI
	endif()

	target_link_libraries(${CMAKE_PROJECT_NAME} OBS::w32-pthreads)
endif()

if(OS_LINUX)
	target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra)
	target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,-z,defs)
	target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--unresolved-symbols=report-all)
	if(ENABLE_COVERAGE)
		target_compile_options(${PROJECT_NAME} PRIVATE -coverage)
		target_link_options(${PROJECT_NAME} PRIVATE -coverage)
	endif()

	set_source_files_properties(
		deps/quirc/lib/identify.c
		PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare"
	)
	set_source_files_properties(
		deps/quirc/lib/quirc.c
		PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare"
	)
endif()

if(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -fvisibility=default")

	set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PREFIX "")
	set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
	set(MACOSX_PLUGIN_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
	set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")
endif()

setup_plugin_target(${CMAKE_PROJECT_NAME})

configure_file(installer/installer-macOS.pkgproj.in installer-macOS.generated.pkgproj)
