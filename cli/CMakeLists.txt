add_executable(dentry_cli
    src/main.cpp
    src/shell.cpp)

target_link_libraries(dentry_cli PRIVATE dentry_core)

target_compile_definitions(dentry_cli PRIVATE DENTRY_VERSION="${PROJECT_VERSION}")

install(TARGETS dentry_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dentry_cli PRIVATE -Wall -Wextra -Wpedantic)
    if(DENTRY_WARNINGS_AS_ERRORS)
        target_compile_options(dentry_cli PRIVATE -Werror)
    endif()
endif()

set_target_properties(dentry_cli PROPERTIES OUTPUT_NAME dentry)
