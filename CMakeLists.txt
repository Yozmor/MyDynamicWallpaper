cmake_minimum_required(VERSION 3.15)
project(MyDynamicWallpaper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_definitions(UNICODE _UNICODE)
    add_compile_options("/utf-8")
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SOURCES
    src/main.cpp
    src/theme.cpp
    src/wallpaper.cpp
    src/trayicon.cpp
    src/settings.cpp
    src/astro.cpp
    src/ui.cpp
    src/resources.rc
)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

target_link_libraries(${PROJECT_NAME} ole32 comdlg32 shell32 uuid msimg32 gdiplus uxtheme)

# Копируем папку themes в каталог сборки
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/themes
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/themes
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/cities.json
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/cities.json
)
# Конструктор тем (графический сборщик)
add_executable(ThemeBuilderGUI WIN32
    tools/theme_builder_gui.cpp
    tools/theme_builder_resources.rc
)
target_include_directories(ThemeBuilderGUI PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ThemeBuilderGUI PRIVATE comctl32 shlwapi shell32 gdiplus)
target_compile_features(ThemeBuilderGUI PRIVATE cxx_std_17)