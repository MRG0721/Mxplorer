add_executable(fman_cli
    src/main.cpp
    src/shell.cpp)

target_link_libraries(fman_cli PRIVATE fman_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fman_cli PRIVATE -Wall -Wextra -Wpedantic)
    if(FMAN_WARNINGS_AS_ERRORS)
        target_compile_options(fman_cli PRIVATE -Werror)
    endif()
endif()

set_target_properties(fman_cli PROPERTIES OUTPUT_NAME fman)
