cmake_minimum_required(VERSION 3.31.6)
project(nmc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

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

# Optional: Add compiler warnings for best practices
if (CMAKE_CXX_COMPILER_ID MATCHES "(GNU|Clang)")
    add_compile_options(
        -Wall          # Enable all warnings
        -Wextra        # Enable extra warnings
        -Wpedantic     # Conformance to the C++ standard
        -Werror        # Treat warnings as errors (good for CI/CD)
    )
endif()

# Link any necessary libraries (e.g., for HTTP requests, JSON parsing)
# For a real application, you might add:
# find_package(Curl REQUIRED)
# target_link_libraries(nmc PRIVATE ${CURL_LIBRARIES})
# find_package(JsonCpp REQUIRED) # Or any other JSON library
# target_link_libraries(nmc PRIVATE JsonCpp::JsonCpp)