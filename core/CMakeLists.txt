# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 mrg

add_library(mxplorer_core STATIC
    src/directory.cpp
    src/entry.cpp
    src/error.cpp
    src/operations.cpp
    src/path_utils.cpp
    src/search.cpp
    src/session.cpp)

target_include_directories(mxplorer_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mxplorer_core PRIVATE -Wall -Wextra -Wpedantic)
    if(MXPLORER_WARNINGS_AS_ERRORS)
        target_compile_options(mxplorer_core PRIVATE -Werror)
    endif()
endif()
