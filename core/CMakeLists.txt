# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 MRG0721

add_library(mxplorer_core STATIC
    src/directory.cpp
    src/entry.cpp
    src/error.cpp
    src/operations.cpp
    src/path_utils.cpp
    src/search.cpp
    src/session.cpp)

target_include_directories(mxplorer_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

mxplorer_enable_warnings(mxplorer_core)
