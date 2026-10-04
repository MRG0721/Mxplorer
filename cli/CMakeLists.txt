# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 MRG0721

add_executable(mxplorer_cli
    src/main.cpp
    src/shell.cpp)

target_link_libraries(mxplorer_cli PRIVATE mxplorer_core)

target_compile_definitions(mxplorer_cli PRIVATE MXPLORER_VERSION="${PROJECT_VERSION}")

install(TARGETS mxplorer_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mxplorer_cli PRIVATE -Wall -Wextra -Wpedantic)
    if(MXPLORER_WARNINGS_AS_ERRORS)
        target_compile_options(mxplorer_cli PRIVATE -Werror)
    endif()
endif()

set_target_properties(mxplorer_cli PROPERTIES OUTPUT_NAME mxplorer)
