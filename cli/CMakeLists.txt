# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 MRG0721

add_executable(mxplorer_cli
    src/main.cpp
    src/shell.cpp
    src/completion.cpp
    src/options.cpp
    src/terminal_text_width.cpp
    src/line_editor.cpp)

target_link_libraries(mxplorer_cli PRIVATE mxplorer_core)

target_compile_definitions(mxplorer_cli PRIVATE MXPLORER_VERSION="${MXPLORER_VERSION_STRING}")

install(TARGETS mxplorer_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

mxplorer_enable_warnings(mxplorer_cli)

set_target_properties(mxplorer_cli PROPERTIES OUTPUT_NAME mxplorer)
