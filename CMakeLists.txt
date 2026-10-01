cmake_minimum_required(VERSION 3.18)
project(MaxLabel
    VERSION 0.1.0
    DESCRIPTION "PFML (Pronunciation Flow Markup Language) editing and proofreading tool"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
# Release bundles are copied out of build/bin verbatim, so binaries must find
# their sibling shared libraries relative to themselves.
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

if(MSVC)
    # /EHsc's 'c' means "assume extern "C" never throws", which is wrong for
    # ggml's API and deletes try/catch blocks around backend init.
    string(REPLACE "/EHsc" "/EHs" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
    set(MAXLABEL_WARN_FLAGS /W3 /wd4244 /wd4267 /wd4996)
else()
    set(MAXLABEL_WARN_FLAGS -Wall -Wextra -Wno-unused-parameter)
endif()

# -----------------------------------------------------------------------------
# tifa_ggml_g2p — the G2P pipeline and the PFML parser/serializer, with no ggml.
#
# MaxLabel is the step *before* the aligner, so it needs G2P and PFML but not
# the model, a backend or a GGUF.  A sibling checkout is used when present (the
# development layout); otherwise the dependency is fetched.
#
# The pin is the branch that introduces the public G2P API — move it to a tag
# once that lands on main.
# -----------------------------------------------------------------------------
set(TIFA_GGML_BUILD_MODEL     OFF CACHE BOOL "" FORCE)
set(TIFA_GGML_BUILD_CLI       OFF CACHE BOOL "" FORCE)
set(TIFA_GGML_BUILD_G2P_TESTS OFF CACHE BOOL "" FORCE)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../tifa.cpp/CMakeLists.txt")
    message(STATUS "MaxLabel: using the sibling tifa.cpp checkout")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../tifa.cpp" tifa.cpp)
else()
    message(STATUS "MaxLabel: fetching tifa.cpp")
    include(FetchContent)
    FetchContent_Declare(tifa_ggml_g2p_src
        GIT_REPOSITORY https://github.com/KakaruHayate/tifa.cpp.git
        GIT_TAG        feat/g2p-public-api)
    FetchContent_MakeAvailable(tifa_ggml_g2p_src)
endif()

# -----------------------------------------------------------------------------
# Core library — the project model.  No Qt: the data layer is usable (and
# testable) on its own, which is also what keeps it buildable in CI without a
# Qt install.
# -----------------------------------------------------------------------------
add_library(maxlabel_core STATIC
    src/core/core.cpp
)
add_library(MaxLabel::core ALIAS maxlabel_core)

target_include_directories(maxlabel_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(maxlabel_core PUBLIC tifa_ggml::g2p)
target_compile_options(maxlabel_core PRIVATE ${MAXLABEL_WARN_FLAGS})

# std::filesystem needs an explicit library on GCC 8.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(maxlabel_core PUBLIC stdc++fs)
endif()

# -----------------------------------------------------------------------------
# CLI
# -----------------------------------------------------------------------------
add_executable(maxlabel_cli src/cli/main.cpp)
target_link_libraries(maxlabel_cli PRIVATE MaxLabel::core)
target_compile_options(maxlabel_cli PRIVATE ${MAXLABEL_WARN_FLAGS})

if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    foreach(_crt IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET maxlabel_cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_crt}" "$<TARGET_FILE_DIR:maxlabel_cli>")
    endforeach()
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
option(MAXLABEL_BUILD_TESTS "Build the MaxLabel self-tests" ON)
if(MAXLABEL_BUILD_TESTS)
    enable_testing()
    add_executable(maxlabel_core_test tests/test_core.cpp)
    target_link_libraries(maxlabel_core_test PRIVATE MaxLabel::core)
    target_compile_options(maxlabel_core_test PRIVATE ${MAXLABEL_WARN_FLAGS})
    add_test(NAME maxlabel_core COMMAND maxlabel_core_test)
endif()

# -----------------------------------------------------------------------------
# GUI (Qt6).  Optional: the core and the CLI build without Qt, which keeps the
# data layer independent of a Qt install.
#
# MAXLABEL_BUILD_UI=ON means "build the GUI", so a missing Qt is an error
# rather than a silent downgrade — otherwise a CI job could pass without ever
# compiling the GUI it claims to check.  Local builds without Qt pass OFF.
# -----------------------------------------------------------------------------
option(MAXLABEL_BUILD_UI "Build the Qt6 GUI (OFF builds the core and CLI only)" ON)
if(MAXLABEL_BUILD_UI)
    find_package(Qt6 COMPONENTS Widgets REQUIRED)
    add_subdirectory(src/ui)
endif()

message(STATUS "")
message(STATUS "MaxLabel ${PROJECT_VERSION} configuration:")
message(STATUS "  GUI (Qt6) .......... ${MAXLABEL_BUILD_UI}")
message(STATUS "  Tests .............. ${MAXLABEL_BUILD_TESTS}")
message(STATUS "")
