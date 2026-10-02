# The Qt6 GUI.  Reached only when MAXLABEL_BUILD_UI is ON *and* Qt6 was found,
# so the core library and the CLI stay buildable without a Qt install.

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(MaxLabel
    main.cpp
    MainWindow.cpp
    MainWindow.h
    PronunciationDialog.cpp
    PronunciationDialog.h
)

target_link_libraries(MaxLabel PRIVATE
    MaxLabel::core
    Qt6::Widgets
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
