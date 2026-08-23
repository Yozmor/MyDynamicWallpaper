#pragma once
#include <string>

class Settings {
public:
    // Загрузка настроек из файла (если файл отсутствует, создаются значения по умолчанию)
    bool load();

    // Сохранение настроек в файл
    bool save() const;

    // Геттеры и сеттеры
    std::wstring getThemePath() const { return themePath; }
    void setThemePath(const std::wstring& path) { themePath = path; }

    bool getAutoStart() const { return autoStart; }
    void setAutoStart(bool value) { autoStart = value; }

    double getLatitude() const { return latitude; }
    void setLatitude(double value) { latitude = value; }

    double getLongitude() const { return longitude; }
    void setLongitude(double value) { longitude = value; }

    bool isLocationConfigured() const { return locationConfigured; }
    void setLocationConfigured(bool value) { locationConfigured = value; }

private:
    std::wstring themePath;
    bool autoStart = false;
    double latitude = 0.0;
    double longitude = 0.0;
    bool locationConfigured = false;
};