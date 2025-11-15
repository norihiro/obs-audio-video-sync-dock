cmake_minimum_required(VERSION 3.12)

project(obs-audio-video-sync-dock VERSION 0.1.1)

set(PLUGIN_AUTHOR "Norihiro Kamae")

set(MACOS_BUNDLEID "net.nagater.obs-audio-video-sync-dock")
set(ID_PREFIX "net.nagater.obs-audio-video-sync-dock.")
set(MACOS_PACKAGE_UUID "10FED78A-2D7F-494B-A7B0-780280CB09B8")
set(MACOS_INSTALLER_UUID "C0ADE8B3-B56D-4328-BEAF-0A967FED280B")
set(PLUGIN_URL "https://github.com/norihiro/obs-audio-video-sync-dock/")
set(WIN_INSTALLER_UUID "C379B62A-A937-4808-B143-A4D4D454A2CC")

set(LINUX_MAINTAINER_EMAIL "norihiro@nagater.net")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
	find_package(libobs REQUIRED)
	find_package(obs-frontend-api REQUIRED)
	include(cmake/ObsPluginHelpers.cmake)
	add_library(OBS::frontend-api ALIAS OBS::obs-frontend-api)
	find_qt(VERSION ${QT_VERSION} COMPONENTS Widgets Core Gui)
endif()

configure_file(
	src/plugin-macros.h.in
	plugin-macros.generated.h
)

set(PLUGIN_SOURCES
	src/plugin-main.c
	src/sync-test-output.cpp
	src/sync-test-dock.cpp
	src/sync-test-monitor.c
	src/dock-compat.cpp
	deps/quirc/lib/decode.c
	deps/quirc/lib/identify.c
	deps/quirc/lib/quirc.c
	deps/quirc/lib/version_db.c
)

add_library(${PROJECT_NAME} MODULE ${PLUGIN_SOURCES})

target_link_libraries(${PROJECT_NAME}
	OBS::libobs
	OBS::frontend-api
	Qt::Core
	Qt::Widgets
	Qt::Gui
)

option(ENABLE_PROFILE "Enable profile" OFF)
if(ENABLE_PROFILE)
	target_compile_definitions(${PROJECT_NAME} PRIVATE "-DENABLE_PROFILE")
endif()

target_include_directories(${PROJECT_NAME}
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
		add_definitions("-Dstrtok_r=strtok_s")
	endif()

	target_link_libraries(${PROJECT_NAME} OBS::w32-pthreads)

	set(LICENSE_DESTINATION "data/obs-plugins/${PROJECT_NAME}")
endif()

if(OS_LINUX)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
	target_link_options(${PROJECT_NAME} PRIVATE -Wl,-z,defs)
	target_link_options(${PROJECT_NAME} PRIVATE -Wl,--unresolved-symbols=report-all)

	set(LICENSE_DESTINATION ${CMAKE_INSTALL_DOCDIR})

	if(ENABLE_COVERAGE)
		target_compile_options(${PROJECT_NAME} PRIVATE -coverage -fprofile-update=atomic)
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

	set(LICENSE_DESTINATION "${PROJECT_NAME}.plugin/Contents/Resources")

	set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
	set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
	set(MACOSX_PLUGIN_BUNDLE_VERSION "${PROJECT_VERSION}")
	set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")
endif()

file(GENERATE OUTPUT .gitignore CONTENT "*\n")

setup_plugin_target(${PROJECT_NAME})

option(INSTALL_LICENSE_FILES "Install license files" ON)
if(INSTALL_LICENSE_FILES)
	install(FILES
		LICENSE
		DESTINATION ${LICENSE_DESTINATION}
	)
	install(FILES
		deps/quirc/LICENSE
		RENAME quirc-LICENSE
		DESTINATION ${LICENSE_DESTINATION}
	)
endif()

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
	configure_file(
		installer/installer-Windows.iss.in
		installer-Windows.generated.iss
	)

	configure_file(
		ci/ci_includes.sh.in
		ci/ci_includes.generated.sh
	)
	configure_file(
		ci/ci_includes.cmd.in
		ci/ci_includes.generated.cmd
	)

	configure_file(
		installer/installer-macOS.pkgproj.in
		installer-macOS.generated.pkgproj
	)
endif()
