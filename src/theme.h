#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

enum class SectorType { Dawn, Day, Dusk, Night };

struct Sector {
    SectorType type;
    std::wstring image;
};

struct Season {
    std::wstring name;   // <-- добавлено поле для имени сезона
    std::vector<Sector> sectors;
};

class Theme {
public:
    bool loadFromFile(const std::wstring& filePath);

    void setLocation(double latitude, double longitude, double timezoneOffset);
    void setDuskAngles(double dawnAngle, double duskAngle);

    std::wstring getImageFor(const SYSTEMTIME& now) const;
    bool isLoaded() const { return m_loaded; }

    size_t getSeasonCount() const { return m_seasons.size(); }
    const std::vector<Sector>& getSeasonSectors(size_t seasonIndex) const {
        static const std::vector<Sector> empty;
        if (seasonIndex < m_seasons.size()) return m_seasons[seasonIndex].sectors;
        return empty;
    }

    // Новый метод: возвращает имя сезона по индексу
    const std::wstring& getSeasonName(size_t index) const;

private:
    struct ActiveSector {
        double startMinute;
        double endMinute;
        std::wstring image;
    };

    std::vector<ActiveSector> buildActiveSectors(const SYSTEMTIME& now, const Season& season) const;

    std::wstring computeImage(const SYSTEMTIME& now) const;
    bool parseJson(const std::string& jsonStr);
    bool extractDwwToTemp(const std::wstring& dwwPath, std::wstring& tempDir);

    static int daysInMonth(int year, int month);
    static int monthFromMarch(int month);

    std::wstring m_basePath;
    std::vector<Season> m_seasons;
    double m_dawnAngle = -12.0;
    double m_duskAngle = -12.0;
    double m_minSectorFraction = 0.25;
    double m_latitude = 55.7558;
    double m_longitude = 37.6176;
    double m_timezone = 3.0;
    bool m_loaded = false;
    std::wstring m_tempDir;
};