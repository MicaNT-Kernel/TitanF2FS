cmake_minimum_required(VERSION 3.24)
project(TitanF2FS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler definitions and flags
if (WIN32)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

if (MSVC)
    add_compile_options(/W4 /permissive- /Zc:preprocessor)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-variable -Wno-unused-but-set-variable)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-Wno-packed-not-aligned)
    endif()
    if (WIN32)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Standalone TitanF2FS Core Library
add_library(titanf2fs_core INTERFACE)
target_include_directories(titanf2fs_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Test Suite Executable
add_executable(titanf2fs_tests tests/test_f2fs.cpp)
target_link_libraries(titanf2fs_tests PRIVATE titanf2fs_core)

# CLI Formatting Tool
add_executable(titanf2fs_format src/tools/titanf2fs_format.cpp)
target_link_libraries(titanf2fs_format PRIVATE titanf2fs_core)

# CLI Inspection & Scrubber Tool
add_executable(titanf2fs_check src/tools/titanf2fs_check.cpp)
target_link_libraries(titanf2fs_check PRIVATE titanf2fs_core)

# Background Service Daemon
add_executable(titanf2fs_svc src/service/titanf2fs_svc.cpp)
target_link_libraries(titanf2fs_svc PRIVATE titanf2fs_core)
if (WIN32)
    target_link_libraries(titanf2fs_svc PRIVATE advapi32)
endif()

if (WIN32)
    # WinFsp Mount Driver Bridge
    add_executable(titanf2fs_mount src/mount/winfsp_mount.cpp)
    target_link_libraries(titanf2fs_mount PRIVATE titanf2fs_core advapi32)

    # Standalone Installer Packager
    add_executable(titanf2fs_packager src/tools/titanf2fs_packager.cpp)
    target_link_libraries(titanf2fs_packager PRIVATE titanf2fs_core)

    # Standalone 1-Click GUI Installer
    add_executable(titanf2fs_setup WIN32 src/installer/titanf2fs_setup.cpp)
    target_link_libraries(titanf2fs_setup PRIVATE titanf2fs_core user32 gdi32 gdiplus comctl32 dwmapi uxtheme ole32 oleaut32 shell32 advapi32)
    add_dependencies(titanf2fs_setup titanf2fs_format titanf2fs_check titanf2fs_mount titanf2fs_svc titanf2fs_packager)

    set(WINFSP_MSI "${CMAKE_CURRENT_SOURCE_DIR}/redist/winfsp-2.1.25156.msi")
    set(PACKAGER_EXTRA_ARGS "")
    if (EXISTS "${WINFSP_MSI}")
        set(PACKAGER_EXTRA_ARGS "${WINFSP_MSI}")
    endif()

    add_custom_command(
        TARGET titanf2fs_setup POST_BUILD
        COMMAND titanf2fs_packager $<TARGET_FILE:titanf2fs_setup>
                $<TARGET_FILE:titanf2fs_format>
                $<TARGET_FILE:titanf2fs_check>
                $<TARGET_FILE:titanf2fs_mount>
                $<TARGET_FILE:titanf2fs_svc>
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/titanf2fs_hero.jpg
                ${PACKAGER_EXTRA_ARGS}
        COMMENT "Embedding all binaries, WinFsp MSI, and assets into standalone titanf2fs_setup.exe..."
    )
endif()

# ============================================================================
# CPack Packaging Configuration
# ============================================================================
set(CPACK_PACKAGE_NAME "TitanF2FS")
set(CPACK_PACKAGE_VENDOR "MicaNT-Kernel")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Flash-Friendly File System (F2FS) Storage Engine and WinFsp Mount Bridge for Windows NT")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_GENERATOR "ZIP")

install(TARGETS titanf2fs_tests titanf2fs_format titanf2fs_check titanf2fs_svc RUNTIME DESTINATION bin)
if (WIN32)
    install(TARGETS titanf2fs_mount titanf2fs_setup RUNTIME DESTINATION bin)
    install(DIRECTORY assets/ DESTINATION assets)
    install(DIRECTORY redist/ DESTINATION redist OPTIONAL)
endif()

include(CPack)
