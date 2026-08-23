#pragma once
#include <string>

class WallpaperChanger {
public:
    // Устанавливает обои на все мониторы
    static bool setWallpaper(const std::wstring& imagePath);
};