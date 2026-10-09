@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ==========================================================
echo   Сборка MyDynamicWallpaper.exe и ThemeBuilderGUI.exe
echo ==========================================================
echo.

where g++ >nul 2>&1
if errorlevel 1 goto NOGPP
where windres >nul 2>&1
if errorlevel 1 goto NOGPP

if not exist build mkdir build
if not exist program mkdir program

echo [1/4] Иконка и окна программы...
windres -c 1251 -I src src/resources.rc -O coff -o build/app.res
if errorlevel 1 goto FAIL

echo [2/4] MyDynamicWallpaper.exe...
g++ -std=c++17 -O2 -static -mwindows -DUNICODE -D_UNICODE -Iinclude ^
    src/main.cpp src/theme.cpp src/wallpaper.cpp src/trayicon.cpp src/settings.cpp src/astro.cpp src/ui.cpp ^
    build/app.res -o program/MyDynamicWallpaper.exe ^
    -lole32 -lcomdlg32 -lshell32 -luuid -lmsimg32 -lgdiplus -luxtheme -lcomctl32 -lshlwapi
if errorlevel 1 goto FAIL

echo [3/4] Иконка конструктора тем...
windres -I tools tools/theme_builder_resources.rc -O coff -o build/theme_builder.res
if errorlevel 1 goto FAIL

echo [4/4] ThemeBuilderGUI.exe...
g++ -std=c++17 -O2 -static -mwindows -DUNICODE -D_UNICODE -Iinclude ^
    tools/theme_builder_gui.cpp build/theme_builder.res -o program/ThemeBuilderGUI.exe ^
    -lcomctl32 -lshlwapi -lshell32 -lgdiplus -lole32 -lcomdlg32 -luuid -luxtheme
if errorlevel 1 goto FAIL

rmdir /s /q build
echo.
echo [OK] Готово. Программа лежит в папке program.
echo.
pause
exit /b 0

:NOGPP
echo Не найден g++ или windres (MinGW-w64).
echo Поставь MSYS2 или winlibs.com и добавь папку bin в PATH.
echo Можно собрать и через Visual Studio: cmake -B build ^&^& cmake --build build --config Release
pause
exit /b 1

:FAIL
echo.
echo [ОШИБКА] Сборка не удалась, смотри текст выше.
pause
exit /b 1
