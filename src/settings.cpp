#include "settings.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace {
    std::wstring GetSettingsFilePath() {
        wchar_t appData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
            std::wstring dir = std::wstring(appData) + L"\\MyDynamicWallpaper";
            CreateDirectoryW(dir.c_str(), nullptr);
            return dir + L"\\settings.json";
        }
        return L"settings.json"; // запасной вариант
    }
}

bool Settings::load() {
    std::wstring filePath = GetSettingsFilePath();
    std::ifstream file(filePath);
    if (!file.is_open()) {
        // Файла нет — оставляем значения по умолчанию
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonStr = buffer.str();

    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);

        if (j.contains("themePath")) {
            std::string p = j["themePath"].get<std::string>();
            themePath = std::wstring(p.begin(), p.end());
        }
        if (j.contains("autoStart")) {
            autoStart = j["autoStart"].get<bool>();
        }
        if (j.contains("latitude")) {
            latitude = j["latitude"].get<double>();
        }
        if (j.contains("longitude")) {
            longitude = j["longitude"].get<double>();
        }
        if (j.contains("locationConfigured")) 
        {
            locationConfigured = j["locationConfigured"].get<bool>();
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool Settings::save() const {
    nlohmann::json j;
    j["themePath"] = std::string(themePath.begin(), themePath.end());
    j["autoStart"] = autoStart;
    j["latitude"] = latitude;
    j["longitude"] = longitude;
    j["locationConfigured"] = locationConfigured;

    std::wstring filePath = GetSettingsFilePath();
    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    file << j.dump(4);
    return true;
}