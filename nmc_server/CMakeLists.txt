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

# --- Build Zstandard (libzstd) using make ---
include(ExternalProject)

set(ZSTD_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/zstd-install")
ExternalProject_Add(
        zstd_project
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG        v1.5.7
        PREFIX         "${CMAKE_BINARY_DIR}/_deps/zstd"
        # Zstd has its own Makefile; no configure step needed
        CONFIGURE_COMMAND ""
        BUILD_COMMAND    make -C "<SOURCE_DIR>"
        INSTALL_COMMAND  make -C "<SOURCE_DIR>" PREFIX="${ZSTD_INSTALL_DIR}" install
        STEP_TARGETS     build install
)

# Create an IMPORTED target for the static lib
add_library(zstd STATIC IMPORTED GLOBAL)
set_target_properties(zstd PROPERTIES
        IMPORTED_LOCATION "${ZSTD_INSTALL_DIR}/lib/libzstd.a"
        INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INSTALL_DIR}/include"
)
add_library(zstd::libzstd ALIAS zstd)
add_library(zstd::libzstd_shared ALIAS zstd)
add_library(zstd::libzstd_static ALIAS zstd)
add_dependencies(zstd zstd_project)
list(APPEND CMAKE_PREFIX_PATH "${ZSTD_INSTALL_DIR}")

# --- Now configure httplib to pick up our make‐built ZSTD ---
FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.21.0
        CMAKE_ARGS
        -DCMAKE_POLICY_VERSION_MINIMUM=3.14
        -DHTTPLIB_USE_ZSTD_IF_AVAILABLE=ON
        -DHTTPLIB_REQUIRE_ZSTD=ON
        -DZSTD_ROOT=${ZSTD_INSTALL_DIR}
        -DZSTD_LIBRARY=${ZSTD_INSTALL_DIR}/lib/libzstd.a
        -DZSTD_INCLUDE_DIR=${ZSTD_INSTALL_DIR}/include
)
FetchContent_MakeAvailable(httplib)

# --- Find OpenSSL (typically system-wide) ---
# httplib already depends on OpenSSL, so this should already be present.
# It's important to find OpenSSL BEFORE libwebsockets if libwebsockets's CMake
# expects it to be available via find_package.
find_package(OpenSSL REQUIRED)

# Logger - https://github.com/gabime/spdlog
find_package(spdlog REQUIRED)

# --- Build and Install libyaml (Autotools project) ---
# This library is a prerequisite for the Kubernetes C client.
# Since libyaml at this tag is an autotools project, we use ExternalProject_Add
# to execute the bootstrap, configure, make, and make install commands.
set(LIBYAML_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/libyaml_project-install")
file(MAKE_DIRECTORY "${LIBYAML_INSTALL_DIR}/include")
file(MAKE_DIRECTORY "${LIBYAML_INSTALL_DIR}/lib")
ExternalProject_Add(
        libyaml_project
        GIT_REPOSITORY https://github.com/yaml/libyaml.git
        GIT_TAG        "0.2.5"
        SOURCE_DIR     "${CMAKE_BINARY_DIR}/_deps/libyaml_project-src"
        BINARY_DIR     "${CMAKE_BINARY_DIR}/_deps/libyaml_project-build"
        INSTALL_DIR    "${LIBYAML_INSTALL_DIR}"
        CONFIGURE_COMMAND
        "${CMAKE_COMMAND}" -E chdir "<SOURCE_DIR>" ./bootstrap
        COMMAND "${CMAKE_COMMAND}" -E chdir "<BINARY_DIR>" "<SOURCE_DIR>/configure" --prefix=<INSTALL_DIR> --enable-shared --disable-static
        BUILD_COMMAND     "${CMAKE_COMMAND}" -E chdir "<BINARY_DIR>" make
        INSTALL_COMMAND   "${CMAKE_COMMAND}" -E chdir "<BINARY_DIR>" make install
        STEP_TARGETS configure build install
)
file(MAKE_DIRECTORY "${LIBYAML_INSTALL_DIR}/lib/cmake/yaml")
file(WRITE "${LIBYAML_INSTALL_DIR}/lib/cmake/yaml/yamlConfig.cmake"
        "set(YAML_FOUND TRUE)\n"
        "set(YAML_INCLUDE_DIRS \"${LIBYAML_INSTALL_DIR}/include\")\n"
        "set(YAML_LIBRARIES \"${LIBYAML_INSTALL_DIR}/lib/libyaml.so\")\n"
        "message(STATUS \"Found YAML (via custom config) in ${LIBYAML_INSTALL_DIR}/include\")\n"
)
message(STATUS "Generated dummy yamlConfig.cmake at: ${LIBYAML_INSTALL_DIR}/lib/cmake/yaml/yamlConfig.cmake")
message(STATUS "LibYAML installed to: ${LIBYAML_INSTALL_DIR}")

add_library(yaml INTERFACE IMPORTED)
set_target_properties(yaml PROPERTIES
        INTERFACE_LINK_LIBRARIES "${LIBYAML_INSTALL_DIR}/lib/libyaml.so"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBYAML_INSTALL_DIR}/include"
)
add_dependencies(yaml libyaml_project)
list(APPEND CMAKE_PREFIX_PATH "${LIBYAML_INSTALL_DIR}")

# --- Fetch libwebsockets ---
# This is a client-side websocket library, also a dependency of the Kubernetes C client.
# Ensure it is built with OpenSSL support.
FetchContent_Declare(
        libwebsockets_ext
        GIT_REPOSITORY https://github.com/warmcat/libwebsockets.git
        GIT_TAG        v4.3.5
        SOURCE_SUBDIR  cmake
        CMAKE_ARGS
        -DLWS_WITH_SSL=ON
        -DLWS_WITH_EXTERNAL_POLL=ON
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
        -DCMAKE_FIND_PACKAGE_NO_SYSTEM_PATHS=TRUE
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
        SOURCE_SUBDIR kubernetes
        CMAKE_ARGS
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POLICY_VERSION_MINIMUM=3.14
        -DCMAKE_FIND_PACKAGE_NO_SYSTEM_PATHS=TRUE
        -DCMAKE_PREFIX_PATH=${LIBYAML_INSTALL_DIR};${CMAKE_BINARY_DIR}/_deps/libwebsockets_ext-build/install
        -Dyaml_DIR=${LIBYAML_INSTALL_DIR}/lib/cmake/yaml
        -Dlibwebsockets_DIR=${CMAKE_BINARY_DIR}/_deps/libwebsockets_ext-build/install/lib/cmake/libwebsockets
        -DYAML_FOUND=TRUE
        -DYAML_LIBRARIES=${LIBYAML_INSTALL_DIR}/lib/libyaml.so
        -DYAML_INCLUDE_DIRS=${LIBYAML_INSTALL_DIR}/include
        -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=TRUE
        -Dlibwebsockets_ROOT=${CMAKE_BINARY_DIR}/_deps/libwebsockets_ext-build/install
)
FetchContent_MakeAvailable(kubernetes_client_c_ext)
message(STATUS "kubernetes_client_c_ext_SOURCE_DIR: ${kubernetes_client_c_ext_SOURCE_DIR}")

# Add source files based on the provided directory structure
set(NMC_SERVER_SOURCES
        src/main.cpp
        src/Core/APIRoutes.cpp
        src/Core/ConnectionHandlers.cpp
        src/Core/K8sHandlers.cpp
        src/Core/OIDCValidator.cpp
        src/Core/OpenShiftClient.cpp
        src/Core/Utils.cpp
)

# Add the main executable
add_executable(nmc_server ${NMC_SERVER_SOURCES})
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
        nlohmann_json::nlohmann_json
        httplib
        -Wl,--whole-archive kubernetes -Wl,--no-whole-archive
        websockets
        OpenSSL::SSL
        OpenSSL::Crypto
        spdlog::spdlog_header_only
        yaml
        # Link against Zstandard via the imported target
        ${ZSTD_LIBRARIES}
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

# Copy documentation files to the build directory
file(COPY "${CMAKE_SOURCE_DIR}/src/docs"
        DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
