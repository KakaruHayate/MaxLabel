# The Qt6 GUI.  Reached only when MAXLABEL_BUILD_UI is ON *and* Qt6 was found,
# so the core library and the CLI stay buildable without a Qt install.

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(MaxLabel
    main.cpp
    MainWindow.cpp
    MainWindow.h
    Icons.cpp
    Icons.h
    PronunciationDialog.cpp
    PronunciationDialog.h
    resources.qrc
    ../audio/AudioClip.cpp
    ../audio/AudioClip.h
    ../audio/WaveformView.cpp
    ../audio/WaveformView.h
    ../audio/AudioPanel.cpp
    ../audio/AudioPanel.h
)

# The translations are compiled to .qm at build time and embedded, so a release
# stays one directory.  English is the source language: there is nothing to
# install for it, and the strings in the code are what it shows.
qt_add_translations(MaxLabel
    TS_FILES i18n/maxlabel_zh_CN.ts
    RESOURCE_PREFIX "/i18n"
)

target_include_directories(MaxLabel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../audio)

target_link_libraries(MaxLabel PRIVATE
    MaxLabel::core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Svg
)

set_target_properties(MaxLabel PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(MSVC)
    target_compile_options(MaxLabel PRIVATE /W3 /wd4244 /wd4267 /wd4996 /utf-8)
else()
    target_compile_options(MaxLabel PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
