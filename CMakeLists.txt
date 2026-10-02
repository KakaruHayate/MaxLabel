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
# Pinned to a released tag: the public G2P API landed on tifa.cpp's main and is
# tagged v0.1.3.  A branch pin would break the build if the branch were ever
# deleted, which is not a risk worth carrying for a one-line dependency.
# -----------------------------------------------------------------------------
set(TIFA_GGML_BUILD_MODEL     OFF CACHE BOOL "" FORCE)
set(TIFA_GGML_BUILD_CLI       OFF CACHE BOOL "" FORCE)
set(TIFA_GGML_BUILD_G2P_TESTS OFF CACHE BOOL "" FORCE)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../tifa.cpp/CMakeLists.txt")
    message(STATUS "MaxLabel: using the sibling tifa.cpp checkout")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../tifa.cpp" tifa.cpp)
else()
    message(STATUS "MaxLabel: fetching tifa.cpp v0.1.3")
    include(FetchContent)
    FetchContent_Declare(tifa_ggml_g2p_src
        GIT_REPOSITORY https://github.com/KakaruHayate/tifa.cpp.git
        GIT_TAG        v0.1.3)
    FetchContent_MakeAvailable(tifa_ggml_g2p_src)
endif()

# -----------------------------------------------------------------------------
# fastText — the engine behind the language detector.
#
# Only the sources are taken; its own CMakeLists is deliberately not used.  It
# sets `-pthread -std=c++11 -funroll-loops -O3 -march=native` unconditionally
# (so it cannot build with MSVC at all), requires cmake_minimum_required(2.8.9)
# which CMake 4 refuses, and lists main.cc among the library sources — which
# would collide with our own main.  Compiling the sources ourselves avoids all
# three and costs nothing.
#
# The model is not shipped here: lid.176.bin is 125 MB and its weights are
# CC BY-SA 3.0, so it is a file the caller points at, exactly like --vocab.
# -----------------------------------------------------------------------------
option(MAXLABEL_WITH_FASTTEXT "Build the fastText language detector" ON)
if(MAXLABEL_WITH_FASTTEXT)
    include(FetchContent)
    FetchContent_Declare(fasttext_src
        GIT_REPOSITORY https://github.com/facebookresearch/fastText.git
        GIT_TAG        5b5943c118b0ec5fb9cd8d20587de2b2d3966dfe   # v0.9.2
        # A directory with no CMakeLists, so MakeAvailable does not add it.
        SOURCE_SUBDIR  not-used)
    FetchContent_MakeAvailable(fasttext_src)

    file(GLOB FASTTEXT_SOURCES ${fasttext_src_SOURCE_DIR}/src/*.cc)
    list(FILTER FASTTEXT_SOURCES EXCLUDE REGEX "main\\.cc$")

    add_library(maxlabel_fasttext STATIC ${FASTTEXT_SOURCES})
    target_include_directories(maxlabel_fasttext PUBLIC ${fasttext_src_SOURCE_DIR}/src)
    target_compile_definitions(maxlabel_fasttext PRIVATE _CRT_SECURE_NO_WARNINGS)
    # fastText's sources use int64_t and friends without including <cstdint>,
    # relying on a transitive include that older libstdc++ happened to provide
    # and newer ones do not.  Forcing the header in is smaller than patching.
    if(MSVC)
        target_compile_options(maxlabel_fasttext PRIVATE /FIcstdint)
    else()
        # fastText uses std::thread; MinGW needs the posix thread runtime.
        target_compile_options(maxlabel_fasttext PRIVATE -include cstdint -pthread)
        target_link_options(maxlabel_fasttext PUBLIC -pthread)
    endif()
endif()

# -----------------------------------------------------------------------------
# Core library — the project model.  No Qt: the data layer is usable (and
# testable) on its own, which is also what keeps it buildable in CI without a
# Qt install.
# -----------------------------------------------------------------------------
set(MAXLABEL_CORE_SOURCES
    src/core/core.cpp
    src/core/languages.cpp
    src/core/language.cpp
    src/core/langseg.cpp
    src/core/budoux.cpp
    src/core/mini_json.cpp
    src/core/models.cpp
    src/core/annotate.cpp
    src/core/vocabulary.cpp
    src/core/g2p_context.cpp
)
if(MAXLABEL_WITH_FASTTEXT)
    list(APPEND MAXLABEL_CORE_SOURCES src/core/fasttext_detector.cpp)
endif()

add_library(maxlabel_core STATIC ${MAXLABEL_CORE_SOURCES})
add_library(MaxLabel::core ALIAS maxlabel_core)

target_include_directories(maxlabel_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(maxlabel_core PUBLIC tifa_ggml::g2p)
target_compile_options(maxlabel_core PRIVATE ${MAXLABEL_WARN_FLAGS})

if(MAXLABEL_WITH_FASTTEXT)
    target_link_libraries(maxlabel_core PUBLIC maxlabel_fasttext)
    target_compile_definitions(maxlabel_core PUBLIC MAXLABEL_HAS_FASTTEXT=1)
endif()

# Where the data files live when nothing says otherwise: the source tree in a
# development build.  A release overrides it at runtime, relative to the binary.
target_compile_definitions(maxlabel_core PUBLIC
    MAXLABEL_DEFAULT_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models")

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

    add_executable(maxlabel_language_test tests/test_language.cpp)
    target_link_libraries(maxlabel_language_test PRIVATE MaxLabel::core)
    target_compile_options(maxlabel_language_test PRIVATE ${MAXLABEL_WARN_FLAGS})
    add_test(NAME maxlabel_language COMMAND maxlabel_language_test)

    add_executable(maxlabel_vocabulary_test tests/test_vocabulary.cpp)
    target_link_libraries(maxlabel_vocabulary_test PRIVATE MaxLabel::core)
    target_compile_options(maxlabel_vocabulary_test PRIVATE ${MAXLABEL_WARN_FLAGS})
    add_test(NAME maxlabel_vocabulary COMMAND maxlabel_vocabulary_test)

    add_executable(maxlabel_conformance_test tests/test_pfml_conformance.cpp)
    target_link_libraries(maxlabel_conformance_test PRIVATE MaxLabel::core)
    target_compile_options(maxlabel_conformance_test PRIVATE ${MAXLABEL_WARN_FLAGS})
    add_test(NAME maxlabel_pfml_conformance COMMAND maxlabel_conformance_test)

    add_executable(maxlabel_budoux_test tests/test_budoux.cpp)
    target_link_libraries(maxlabel_budoux_test PRIVATE MaxLabel::core)
    target_compile_options(maxlabel_budoux_test PRIVATE ${MAXLABEL_WARN_FLAGS})
    target_compile_definitions(maxlabel_budoux_test PRIVATE
        BUDOUX_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models/budoux")
    add_test(NAME maxlabel_budoux COMMAND maxlabel_budoux_test)
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
    find_package(Qt6 COMPONENTS Widgets Multimedia Svg LinguistTools REQUIRED)
    add_subdirectory(src/ui)
endif()

message(STATUS "")
message(STATUS "MaxLabel ${PROJECT_VERSION} configuration:")
message(STATUS "  GUI (Qt6) .......... ${MAXLABEL_BUILD_UI}")
message(STATUS "  Tests .............. ${MAXLABEL_BUILD_TESTS}")
message(STATUS "")
