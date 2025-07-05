cmake_minimum_required(VERSION 3.14) # Changed to 3.14 for broader FetchContent compatibility
project(nmc_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POLICY_VERSION_MINIMUM 3.14) # Ensure policies are consistent with CMAKE_MINIMUM_REQUIRED
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
set(CMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY TRUE)
set(CMAKE_FIND_PACKAGE_NO_DEFAULT_PATH TRUE)

# Include necessary CMake modules
include(FetchContent)
include(ExternalProject) # Required for building autotools-based projects like libyaml

# --- Fetch nlohmann/json ---
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.12.0
        # Force a modern policy context for the subproject if its CMakeLists.txt is older
        CMAKE_ARGS     -DCMAKE_POLICY_VERSION_MINIMUM=3.14
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Fetch cpp-httplib ---
FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.21.0
        # Force a modern policy context for the subproject
        CMAKE_ARGS     -DCMAKE_POLICY_VERSION_MINIMUM=3.14
)
FetchContent_MakeAvailable(httplib)

# --- Find OpenSSL (typically system-wide) ---
# httplib already depends on OpenSSL, so this should already be present.
# It's important to find OpenSSL BEFORE libwebsockets if libwebsockets's CMake
# expects it to be available via find_package.
find_package(OpenSSL REQUIRED)

# --- Find zstd::libzstd ---
# This is a compression library used by the Kubernetes C client.
find_package(zstd REQUIRED)

# --- Build and Install libyaml (Autotools project) ---
# This library is a prerequisite for the Kubernetes C client.
# Since libyaml at this tag is an autotools project, we use ExternalProject_Add
# to execute the bootstrap, configure, make, and make install commands.

# Define the installation directory for libyaml upfront
set(LIBYAML_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/libyaml_project-install")

# CRITICAL: Ensure the install directories exist *before* the Kubernetes client tries to configure.
# This prevents the "non-existent path" error during the configure stage.
file(MAKE_DIRECTORY "${LIBYAML_INSTALL_DIR}/include")
file(MAKE_DIRECTORY "${LIBYAML_INSTALL_DIR}/lib")

ExternalProject_Add(
        libyaml_project # A unique name for this external project target
        GIT_REPOSITORY https://github.com/yaml/libyaml.git
        GIT_TAG        "0.2.5"
        SOURCE_DIR     "${CMAKE_BINARY_DIR}/_deps/libyaml_project-src" # Where the source will be cloned
        BINARY_DIR     "${CMAKE_BINARY_DIR}/_deps/libyaml_project-build" # Where the build will happen
        INSTALL_DIR    "${LIBYAML_INSTALL_DIR}" # Use the defined variable here directly

        # Commands for autotools build process
        # Bootstrap needs to be run from SOURCE_DIR
        CONFIGURE_COMMAND
        "${CMAKE_COMMAND}" -E chdir "<SOURCE_DIR>" ./bootstrap
        # Configure needs to be run from BINARY_DIR, pointing to the configure script in SOURCE_DIR
        COMMAND "${CMAKE_COMMAND}" -E chdir "<BINARY_DIR>" "<SOURCE_DIR>/configure" --prefix=<INSTALL_DIR> --enable-shared --disable-static
        # Build and Install commands also run from BINARY_DIR
        BUILD_COMMAND     "${CMAKE_COMMAND}" -E chdir "<BINARY_DIR>" make
        INSTALL_COMMAND   "${CMAKE_COMMAND}" -E chdir "<BINARY_DIR>" make install

        # Ensure this project is built before anything that depends on it
        STEP_TARGETS configure build install
)

# CRITICAL: Generate the yamlConfig.cmake file *before* the Kubernetes client is configured.
# This ensures it's available for the nested find_package call during CMake's configure stage.
file(MAKE_DIRECTORY "${LIBYAML_INSTALL_DIR}/lib/cmake/yaml") # Ensure this specific directory exists for the config file
file(WRITE "${LIBYAML_INSTALL_DIR}/lib/cmake/yaml/yamlConfig.cmake"
        "set(YAML_FOUND TRUE)\n"
        "set(YAML_INCLUDE_DIRS \"${LIBYAML_INSTALL_DIR}/include\")\n"
        "set(YAML_LIBRARIES \"${LIBYAML_INSTALL_DIR}/lib/libyaml.so\")\n"
        "message(STATUS \"Found YAML (via custom config) in \${YAML_INCLUDE_DIRS}\")\n"
)
message(STATUS "Generated dummy yamlConfig.cmake at: ${LIBYAML_INSTALL_DIR}/lib/cmake/yaml/yamlConfig.cmake")


# We will pass this installation directory to the Kubernetes client build
# so it can find libyaml.
message(STATUS "LibYAML installed to: ${LIBYAML_INSTALL_DIR}")

# Create an INTERFACE library target for libyaml for linking from our main project.
add_library(yaml INTERFACE IMPORTED)
set_target_properties(yaml PROPERTIES
        INTERFACE_LINK_LIBRARIES "${LIBYAML_INSTALL_DIR}/lib/libyaml.so" # Assuming shared lib
        INTERFACE_INCLUDE_DIRECTORIES "${LIBYAML_INSTALL_DIR}/include"
)
add_dependencies(yaml libyaml_project) # Ensure libyaml is built before this target is used

# Although we are setting CMAKE_ARGS for the sub-project,
# keeping CMAKE_PREFIX_PATH for the main project's general finds is good practice.
list(APPEND CMAKE_PREFIX_PATH "${LIBYAML_INSTALL_DIR}")


# --- Fetch libwebsockets ---
# This is a client-side websocket library, also a dependency of the Kubernetes C client.
# Ensure it is built with OpenSSL support.
FetchContent_Declare(
        libwebsockets_ext
        GIT_REPOSITORY https://github.com/warmcat/libwebsockets.git
        GIT_TAG        v4.3.5 # Matching the version found by the Kubernetes client
        SOURCE_SUBDIR  cmake # Use the cmake subdirectory for the build
        CMAKE_ARGS
            -DLWS_WITH_SSL=ON
            -DLWS_WITH_EXTERNAL_POLL=ON # Required by kubernetes client
            -DLWS_MAX_SMP=1
            -DLWS_WITH_HTTP_PROXY=OFF
            -DLWS_WITH_SHARED=ON
            -DLWS_WITHOUT_TESTAPPS=ON
            -DLWS_WITHOUT_TEST_SERVER=ON
            -DLWS_WITHOUT_TEST_APPS=ON
            -DLWS_WITH_LWSAC=OFF
            -DLWS_OPENSSL_INCLUDE_DIRS=${OpenSSL_INCLUDE_DIR}
            -DLWS_OPENSSL_LIBRARIES=${OpenSSL_LIBRARIES}
            -DCMAKE_POLICY_VERSION_MINIMUM=3.14
            # NEW: Prevent libwebsockets's internal CMake from finding system libraries
            -DCMAKE_FIND_PACKAGE_NO_SYSTEM_PATHS=TRUE
            # NEW: Ensure it prefers its own generated config files if it tries to find itself
            -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=TRUE
)
FetchContent_MakeAvailable(libwebsockets_ext)
find_package(libwebsockets CONFIG REQUIRED)

# --- Fetch Kubernetes C Client Library ---
# This is the main client library. It depends on libyaml, libwebsockets, and OpenSSL.
# Ensure these dependencies are made available by FetchContent_MakeAvailable *before*
# this FetchContent_Declare/MakeAvailable block for the Kubernetes client.
FetchContent_Declare(
        kubernetes_client_c_ext
        GIT_REPOSITORY https://github.com/kubernetes-client/c.git
        GIT_TAG        v0.13.0
        # CRUCIAL: Point to the 'kubernetes' subdirectory where the actual CMakeLists.txt is located
        SOURCE_SUBDIR kubernetes
        # Configure the Kubernetes C client to build as a shared library
        CMAKE_ARGS
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_BUILD_TYPE=Release # Or Debug if you need debug symbols
        -DCMAKE_POLICY_VERSION_MINIMUM=3.14

        # CRITICAL: Instruct CMake to not look in system paths for any packages within this sub-build.
        # This should prevent the system libwebsockets from being found.
        -DCMAKE_FIND_PACKAGE_NO_SYSTEM_PATHS=TRUE

        # Reconstruct CMAKE_PREFIX_PATH for the sub-build to only include our fetched dependencies.
        -DCMAKE_PREFIX_PATH=${LIBYAML_INSTALL_DIR};${CMAKE_BINARY_DIR}/_deps/libwebsockets_ext-build/install

        # Explicitly point to the specific config files (optional but good for robustness)
        -Dyaml_DIR=${LIBYAML_INSTALL_DIR}/lib/cmake/yaml
        -Dlibwebsockets_DIR=${CMAKE_BINARY_DIR}/_deps/libwebsockets_ext-build/install/lib/cmake/libwebsockets

        # Set these for find_package(yaml) internally (already done, keeping for completeness)
        -DYAML_FOUND=TRUE
        -DYAML_LIBRARIES=${LIBYAML_INSTALL_DIR}/lib/libyaml.so
        -DYAML_INCLUDE_DIRS=${LIBYAML_INSTALL_DIR}/include

        # Tell CMake to prefer package configuration files over module files for all find_package calls within the sub-build.
        -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=TRUE

        # You can also set libwebsockets_ROOT to your install path, although CMAKE_PREFIX_PATH should handle it.
        -Dlibwebsockets_ROOT=${CMAKE_BINARY_DIR}/_deps/libwebsockets_ext-build/install
)
FetchContent_MakeAvailable(kubernetes_client_c_ext)

# Add this line to print the source directory
message(STATUS "kubernetes_client_c_ext_SOURCE_DIR: ${kubernetes_client_c_ext_SOURCE_DIR}")

# Add source files based on the provided directory structure
set(NMC_SERVER_SOURCES
        src/main.cpp
        src/Core/APIRoutes.cpp
        src/Core/K8sHandlers.cpp
        src/Core/Utils.cpp
)

add_executable(nmc_server ${NMC_SERVER_SOURCES})

# Ensure libyaml_project is built before nmc_server (it will implicitly be built before kubernetes_client_c_ext too)
add_dependencies(nmc_server libyaml_project)

# Add include directories for internal modules and fetched libraries.
target_include_directories(nmc_server PRIVATE
        ${kubernetes_client_c_ext_SOURCE_DIR}
        ${kubernetes_client_c_ext_SOURCE_DIR}/api
        ${kubernetes_client_c_ext_SOURCE_DIR}/config
        ${kubernetes_client_c_ext_SOURCE_DIR}/include
        ${kubernetes_client_c_ext_SOURCE_DIR}/model
        ${kubernetes_client_c_ext_SOURCE_DIR}/src
        ${kubernetes_client_c_ext_SOURCE_DIR}/watch
        ${kubernetes_client_c_ext_SOURCE_DIR}/websocket
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/Core
        ${CMAKE_SOURCE_DIR}/src/Models
)

# Link against the interface targets provided by the fetched libraries.
target_link_libraries(nmc_server PRIVATE
        # Core dependencies of nmc_server itself
        nlohmann_json::nlohmann_json
        httplib
        -Wl,--whole-archive kubernetes -Wl,--no-whole-archive
        websockets
        OpenSSL::SSL
        OpenSSL::Crypto
        yaml
        zstd::libzstd
)

# Add compiler warnings for best practices
if (CMAKE_CXX_COMPILER_ID MATCHES "(GNU|Clang)")
    add_compile_options(
            -Wall          # Enable all warnings
            -Wextra        # Enable extra warnings
            -Wpedantic     # Conformance to the C++ standard
            -Werror        # Treat warnings as errors (good for CI/CD)
    )
endif()

# --- Copy documentation files to the build directory ---
file(COPY "${CMAKE_SOURCE_DIR}/src/docs"
        DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
