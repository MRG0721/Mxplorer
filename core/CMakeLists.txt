add_library(fman_core STATIC
    src/directory.cpp
    src/entry.cpp
    src/error.cpp
    src/operations.cpp
    src/path_utils.cpp
    src/session.cpp)

target_include_directories(fman_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fman_core PRIVATE -Wall -Wextra -Wpedantic)
    if(FMAN_WARNINGS_AS_ERRORS)
        target_compile_options(fman_core PRIVATE -Werror)
    endif()
endif()
