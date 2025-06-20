cmake_minimum_required(VERSION 3.31.6)
project(nmc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
# Include FetchContent module for automatic dependency downloading
include(FetchContent)

# --- Fetch nlohmann/json ---
# Use FetchContent_Declare to define the repository and tag.
# Use FetchContent_MakeAvailable to configure and build the subproject.
# Add CMAKE_ARGS to specify a policy version compatible with modern CMake,
# which helps avoid errors when the subproject has an old cmake_minimum_required.
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.2 # Using the tag specified by the user
        CMAKE_ARGS     -DCMAKE_POLICY_VERSION_MINIMUM=3.14 # Force a modern policy context for the subproject
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Fetch cpp-httplib ---
# Apply the same logic for httplib.
FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.15.3 # Using the tag specified by the user
        # IMPORTANT: Disable SSL to avoid OpenSSL dependencies in cross-compilation environments
        CMAKE_ARGS     -DCMAKE_POLICY_VERSION_MINIMUM=3.14 -DHTTPPEBLIB_WITH_SSL=OFF
)
FetchContent_MakeAvailable(httplib)

# Define source files
set(NMC_SOURCES
        src/main.cpp
        src/CLI/Command.cpp
        src/CLI/CLIParser.cpp
        src/Commands/BaseCommand.cpp
        src/Commands/RootCommand.cpp
        src/Commands/BucketCommands.cpp
        src/Commands/K8sCommands.cpp
        src/Commands/ModelCommands.cpp
        src/Commands/SSHCommands.cpp
        src/Commands/VersionCommand.cpp
        src/Commands/VMCommands.cpp
        src/Core/CloudAPIClient.cpp
        src/Core/Utils.cpp
)

# Define include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/CLI
    ${CMAKE_SOURCE_DIR}/src/Commands
    ${CMAKE_SOURCE_DIR}/src/Core
    ${CMAKE_SOURCE_DIR}/src/Models
)

add_executable(nmc ${NMC_SOURCES})

# Link against the interface targets provided by the fetched libraries.
# This ensures that their public headers are transitively included and necessary dependencies are met.
target_link_libraries(nmc PRIVATE nlohmann_json::nlohmann_json httplib)

# Optional: Add compiler warnings for best practices
if (CMAKE_CXX_COMPILER_ID MATCHES "(GNU|Clang)")
    add_compile_options(
        -Wall          # Enable all warnings
        -Wextra        # Enable extra warnings
        -Wpedantic     # Conformance to the C++ standard
        -Werror        # Treat warnings as errors (good for CI/CD)
    )
endif()

