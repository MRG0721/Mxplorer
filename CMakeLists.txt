# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 MRG0721

cmake_minimum_required(VERSION 3.20)

project(Mxplorer
    VERSION 0.0.1
    DESCRIPTION "A small file manager with a terminal front-end (Qt6 front-end planned)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Needed in the top level scope: the man page below installs into
# CMAKE_INSTALL_MANDIR, and include() inside a subdirectory would not be
# visible here.
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(MXPLORER_BUILD_CLI "Build the terminal front-end" ON)
option(MXPLORER_BUILD_GUI "Build the Qt6 front-end (does not exist yet)" OFF)
option(MXPLORER_WARNINGS_AS_ERRORS "Turn compiler warnings into errors" OFF)

if(MXPLORER_BUILD_GUI)
    message(FATAL_ERROR
        "The Qt6 front-end is not implemented yet. "
        "Set -DMXPLORER_BUILD_GUI=OFF and see README.md for the roadmap.")
endif()

add_subdirectory(core)

if(MXPLORER_BUILD_CLI)
    add_subdirectory(cli)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/mxplorer.1"
            DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endif()

include(CTest)
if(BUILD_TESTING AND MXPLORER_BUILD_CLI)
    add_test(
        NAME cli_smoke
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_test.sh" "$<TARGET_FILE:mxplorer_cli>")
endif()
