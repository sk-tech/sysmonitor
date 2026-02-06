cmake_minimum_required(VERSION 3.15)

project(SysMonitor 
    VERSION 0.1.0
    DESCRIPTION "Cross-Platform System Monitor & Performance Analyzer"
    LANGUAGES C CXX
)

# C++17 required for filesystem, std::optional, etc.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C11 for low-level system code
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build configuration
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_TESTING "Build tests" ON)
option(BUILD_PYTHON_BINDINGS "Build Python bindings with pybind11" ON)
option(STATIC_LINK_RUNTIME "Statically link C/C++ runtime" ON)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)

# Platform detection
if(WIN32)
    set(PLATFORM_NAME "windows")
    add_definitions(-DPLATFORM_WINDOWS)
elseif(APPLE)
    set(PLATFORM_NAME "macos")
    add_definitions(-DPLATFORM_MACOS)
elseif(UNIX)
    set(PLATFORM_NAME "linux")
    add_definitions(-DPLATFORM_LINUX)
endif()

message(STATUS "Building for platform: ${PLATFORM_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Compiler flags
if(MSVC)
    # MSVC-specific flags
    add_compile_options(/W4 /WX /permissive-)
    if(STATIC_LINK_RUNTIME)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
else()
    # GCC/Clang flags
    add_compile_options(
        -Wall -Wextra -Werror -pedantic
        -Wno-unused-parameter
        -fvisibility=hidden
    )
    
    if(STATIC_LINK_RUNTIME)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    endif()
    
    # Sanitizers for development
    if(ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find dependencies
find_package(Threads REQUIRED)

# SQLite3 - try system first, fallback to bundled
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    message(STATUS "SQLite3 not found, using bundled version")
    add_subdirectory(third_party/sqlite)
endif()

# pybind11 for Python bindings
if(BUILD_PYTHON_BINDINGS)
    find_package(Python3 COMPONENTS Interpreter Development QUIET)
    if(Python3_FOUND)
        add_subdirectory(third_party/pybind11)
        message(STATUS "Python bindings enabled (Python ${Python3_VERSION})")
    else()
        message(WARNING "Python3 not found, disabling Python bindings")
        set(BUILD_PYTHON_BINDINGS OFF)
    endif()
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

# Core library subdirectories
add_subdirectory(src/core)
add_subdirectory(src/platform)
# add_subdirectory(src/collectors)  # Week 2
# add_subdirectory(src/storage)     # Week 2
# add_subdirectory(src/utils)       # Week 2

# Python bindings
if(BUILD_PYTHON_BINDINGS)
    add_subdirectory(src/bindings)
endif()

# Executables
add_subdirectory(src/daemon)
add_subdirectory(src/cli)

# Tests
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(third_party/googletest)
    add_subdirectory(tests)
endif()

# Installation rules
include(GNUInstallDirs)

install(TARGETS sysmond sysmon
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Install configuration files
install(FILES config/sysmon.yaml.example
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/sysmon
    RENAME sysmon.yaml
)

# Install systemd service (Linux only)
if(PLATFORM_NAME STREQUAL "linux")
    install(FILES deploy/systemd/sysmon.service
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system
    )
endif()

# CPack configuration for packaging
set(CPACK_PACKAGE_NAME "sysmonitor")
set(CPACK_PACKAGE_VENDOR "SysMonitor Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-Platform System Monitor")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SysMonitor")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "SysMonitor")
    set(CPACK_NSIS_PACKAGE_NAME "SysMonitor ${PROJECT_VERSION}")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    
    # Debian package settings
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "your-email@example.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.31)")
    
    # RPM package settings
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_REQUIRES "glibc >= 2.31")
endif()

include(CPack)

# Print build summary
message(STATUS "==================== Build Configuration ====================")
message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "Platform: ${PLATFORM_NAME}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Python Bindings: ${BUILD_PYTHON_BINDINGS}")
message(STATUS "Testing: ${BUILD_TESTING}")
message(STATUS "Static Runtime: ${STATIC_LINK_RUNTIME}")
message(STATUS "Sanitizers: ${ENABLE_SANITIZERS}")
message(STATUS "============================================================")
