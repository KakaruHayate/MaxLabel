# The Qt6 GUI.  Reached only when MAXLABEL_BUILD_UI is ON *and* Qt6 was found,
# so the core library and the CLI stay buildable without a Qt install.
#
# The window is a library and the executable is a main(), which is what lets
# the save path be tested by driving the real window rather than a copy of it.

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(maxlabel_ui STATIC
    MainWindow.cpp
    MainWindow.h
    Icons.cpp
    Icons.h
    Resources.cpp
    Resources.h
    PronunciationDialog.cpp
    PronunciationDialog.h
    DialogButtons.cpp
    DialogButtons.h
    LyricEditor.cpp
    LyricEditor.h
    ManualDialog.cpp
    ManualDialog.h
    RunStrip.cpp
    RunStrip.h
    resources.qrc
    ../audio/AudioClip.cpp
    ../audio/AudioClip.h
    ../audio/WaveformView.cpp
    ../audio/WaveformView.h
    ../audio/AudioPanel.cpp
    ../audio/AudioPanel.h
)

target_include_directories(maxlabel_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../audio
)

target_link_libraries(maxlabel_ui PUBLIC
    MaxLabel::core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Svg
)

if(MSVC)
    target_compile_options(maxlabel_ui PRIVATE /W3 /wd4244 /wd4267 /wd4996 /utf-8)
else()
    target_compile_options(maxlabel_ui PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(MaxLabel main.cpp)
target_link_libraries(MaxLabel PRIVATE maxlabel_ui)

set_target_properties(MaxLabel PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# The translations are compiled to .qm at build time and embedded, so a release
# stays one directory.  English is the source language: there is nothing to
# install for it, and the strings in the code are what it shows.
set(MAXLABEL_TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/maxlabel_zh_CN.ts)

if(Qt6LinguistTools_FOUND)
    qt_add_translations(MaxLabel
        TS_FILES ${MAXLABEL_TS_FILES}
        RESOURCE_PREFIX "/i18n"
    )
else()
    # A distribution can ship lrelease without the CMake package that wraps it —
    # Debian puts the config in qt6-tools-dev and the binary in qt6-l10n-tools —
    # so drive the tool directly rather than losing the translations.
    find_program(MAXLABEL_LRELEASE NAMES lrelease lrelease-qt6 lrelease6)
    if(MAXLABEL_LRELEASE)
        set(MAXLABEL_QM ${CMAKE_CURRENT_BINARY_DIR}/maxlabel_zh_CN.qm)
        add_custom_command(OUTPUT ${MAXLABEL_QM}
            COMMAND ${MAXLABEL_LRELEASE} ${MAXLABEL_TS_FILES} -qm ${MAXLABEL_QM}
            DEPENDS ${MAXLABEL_TS_FILES}
            COMMENT "Compiling maxlabel_zh_CN.qm")
        add_custom_target(maxlabel_translations DEPENDS ${MAXLABEL_QM})
        add_dependencies(MaxLabel maxlabel_translations)
        qt_add_resources(MaxLabel "translations"
            PREFIX "/i18n"
            BASE ${CMAKE_CURRENT_BINARY_DIR}
            FILES ${MAXLABEL_QM})
    else()
        message(WARNING
            "MaxLabel: neither Qt LinguistTools nor lrelease was found — "
            "building English only")
    endif()
endif()

if(MAXLABEL_BUILD_TESTS)
    # Drives the real window offscreen.  Everything below the window is covered
    # by the core tests; what is not is an edit made in the editor being
    # written to disk, which is the tool's primary output.
    add_executable(maxlabel_ui_test ${CMAKE_SOURCE_DIR}/tests/test_ui_save.cpp)
    target_link_libraries(maxlabel_ui_test PRIVATE maxlabel_ui)
    add_test(NAME maxlabel_ui_save COMMAND maxlabel_ui_test)
    set_tests_properties(maxlabel_ui_save PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
