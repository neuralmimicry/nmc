# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.31.11)

# Reject any attempt to use a toolchain file. We must not use one because
# we could be downloading it here. If the CMAKE_TOOLCHAIN_FILE environment
# variable is set, the cache variable will have been initialized from it.
unset(CMAKE_TOOLCHAIN_FILE CACHE)
unset(ENV{CMAKE_TOOLCHAIN_FILE})

# We name the project and the target for the ExternalProject_Add() call
# to something that will highlight to the user what we are working on if
# something goes wrong and an error message is produced.

project(kubernetes_client_c_ext-populate NONE)


# Pass through things we've already detected in the main project to avoid
# paying the cost of redetecting them again in ExternalProject_Add()
set(GIT_EXECUTABLE [==[/usr/bin/git]==])
set(GIT_VERSION_STRING [==[2.48.1]==])
set_property(GLOBAL PROPERTY _CMAKE_FindGit_GIT_EXECUTABLE_VERSION
  [==[/usr/bin/git;2.48.1]==]
)


include(ExternalProject)
ExternalProject_Add(kubernetes_client_c_ext-populate
                     "UPDATE_DISCONNECTED" "False" "GIT_REPOSITORY" "https://github.com/kubernetes-client/c.git" "EXTERNALPROJECT_INTERNAL_ARGUMENT_SEPARATOR" "GIT_TAG" "v0.13.0" "SOURCE_SUBDIR" "kubernetes" "CMAKE_ARGS" "-DBUILD_SHARED_LIBS=ON" "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_POLICY_VERSION_MINIMUM=3.14" "-DCMAKE_FIND_PACKAGE_NO_SYSTEM_PATHS=TRUE" "-DCMAKE_PREFIX_PATH=/home/pbisaacs/Developer/neuralmimicry/nmc/nmc_server/build_fresh/_deps/libyaml_project-install" "/usr/lib/x86_64-linux-gnu/cmake/libwebsockets" "-Dyaml_DIR=/home/pbisaacs/Developer/neuralmimicry/nmc/nmc_server/build_fresh/_deps/libyaml_project-install/lib/cmake/yaml" "-DLibwebsockets_DIR=/usr/lib/x86_64-linux-gnu/cmake/libwebsockets" "-Dlibwebsockets_DIR=/usr/lib/x86_64-linux-gnu/cmake/libwebsockets" "-DYAML_FOUND=TRUE" "-DYAML_LIBRARIES=/home/pbisaacs/Developer/neuralmimicry/nmc/nmc_server/build_fresh/_deps/libyaml_project-install/lib/libyaml.so" "-DYAML_INCLUDE_DIRS=/home/pbisaacs/Developer/neuralmimicry/nmc/nmc_server/build_fresh/_deps/libyaml_project-install/include" "-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=TRUE" "-Dlibwebsockets_ROOT=/usr/lib/x86_64-linux-gnu/cmake/libwebsockets"
                    SOURCE_DIR          "/home/pbisaacs/Developer/neuralmimicry/nmc/nmc_server/build_fresh/_deps/kubernetes_client_c_ext-src"
                    BINARY_DIR          "/home/pbisaacs/Developer/neuralmimicry/nmc/nmc_server/build_fresh/_deps/kubernetes_client_c_ext-build"
                    CONFIGURE_COMMAND   ""
                    BUILD_COMMAND       ""
                    INSTALL_COMMAND     ""
                    TEST_COMMAND        ""
                    USES_TERMINAL_DOWNLOAD  YES
                    USES_TERMINAL_UPDATE    YES
                    USES_TERMINAL_PATCH     YES
)


