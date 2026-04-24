# nmc_client/CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" NMC_VERSION_FILE LIMIT_COUNT 1)
if(NOT NMC_VERSION_FILE)
    set(NMC_VERSION_FILE "0.0.3")
endif()
string(STRIP "${NMC_VERSION_FILE}" NMC_VERSION_DEFAULT)
project(nmc VERSION ${NMC_VERSION_DEFAULT} LANGUAGES CXX) # Project name is 'nmc' for the client executable

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POLICY_VERSION_MINIMUM 3.14)
set(NMC_CLIENT_VERSION "${PROJECT_VERSION}" CACHE STRING "NMC client semantic version")
# Include FetchContent module for automatic dependency downloading
include(FetchContent)
include(GNUInstallDirs)
find_package(CURL REQUIRED)

# --- Fetch nlohmann/json ---
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.2
        CMAKE_ARGS     -DCMAKE_POLICY_VERSION_MINIMUM=3.14
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Fetch cpp-httplib ---
FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.15.3
        # IMPORTANT: Disable SSL to avoid OpenSSL dependencies in cross-compilation environments
        # This CMAKE_ARGS is for httplib's internal CMake.
        # We will also add a compile definition below for extra certainty.
        CMAKE_ARGS     -DCMAKE_POLICY_VERSION_MINIMUM=3.14 -DHTTPPEBLIB_WITH_SSL=OFF
)
FetchContent_MakeAvailable(httplib)

# Add source files based on the provided directory structure
set(NMC_CLIENT_SOURCES
        src/main.cpp
        src/CLI/CLIParser.cpp
        src/CLI/Command.cpp
        src/Commands/AarnnCommands.cpp
        src/Commands/BaseCommand.cpp
        src/Commands/BucketCommands.cpp
        src/Commands/ConnectionCommands.cpp
        src/Commands/GailCommands.cpp
        src/Commands/K8sCommands.cpp
        src/Commands/ModelCommands.cpp
        src/Commands/NodeCommands.cpp
        src/Commands/OpenStackCommands.cpp
        src/Commands/OpenShiftCommands.cpp
        src/Commands/ProxmoxCommands.cpp
        src/Commands/RefinerCommands.cpp
        src/Commands/RootCommand.cpp
        src/Commands/ServerCommands.cpp
        src/Commands/SSHCommands.cpp
        src/Commands/TraceyCommands.cpp
        src/Commands/VersionCommand.cpp
        src/Commands/VClusterCommands.cpp
        src/Commands/VMCommands.cpp
        src/Core/CloudAPIClient.cpp
        src/Core/GailClient.cpp
        src/Core/VersionCheck.cpp
        src/Core/Utils.cpp
)

add_executable(nmc ${NMC_CLIENT_SOURCES})

# Explicitly add include directories for internal modules.
target_include_directories(nmc PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/CLI
        ${CMAKE_SOURCE_DIR}/src/Commands
        ${CMAKE_SOURCE_DIR}/src/Core
        ${CMAKE_SOURCE_DIR}/src/Models
)

# Link against the interface targets provided by the fetched libraries.
target_link_libraries(nmc PRIVATE nlohmann_json::nlohmann_json httplib CURL::libcurl)

# Add a compile definition to explicitly disable SSL support in httplib from the client's perspective.
# This ensures that the #ifdef CPPHTTPLIB_OPENSSL_SUPPORT block in httplib.h is skipped.
target_compile_definitions(nmc PRIVATE CPPHTTPLIB_NO_SSL_SUPPORT)
target_compile_definitions(nmc PRIVATE NMC_CLIENT_VERSION="${NMC_CLIENT_VERSION}")

install(TARGETS nmc
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)


# Optional: Add compiler warnings for best practices
if (CMAKE_CXX_COMPILER_ID MATCHES "(GNU|Clang)")
    add_compile_options(
            -Wall          # Enable all warnings
            -Wextra        # Enable extra warnings
            -Wpedantic     # Conformance to the C++ standard
            -Werror        # Treat warnings as errors (good for CI/CD)
    )
endif()
