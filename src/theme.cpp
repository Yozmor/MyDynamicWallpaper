#include "theme.h"
#include "astro.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <windows.h>

namespace fs = std::filesystem;

static std::wstring Utf8ToWstring(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], len);
    result.resize(len - 1);
    return result;
}

bool Theme::parseJson(const std::string& jsonStr) {
    m_seasons.clear();
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonStr);
    } catch (...) {
        return false;
    }

    if (j.contains("sunElevationDawn")) m_dawnAngle = j["sunElevationDawn"].get<double>();
    if (j.contains("sunElevationDusk")) m_duskAngle = j["sunElevationDusk"].get<double>();
    if (j.contains("minSectorFraction")) m_minSectorFraction = j["minSectorFraction"].get<double>();

    const nlohmann::json* seasonsPtr = nullptr;
    if (j.contains("seasons") && j["seasons"].is_array()) seasonsPtr = &j["seasons"];
    else if (j.contains("months") && j["months"].is_array()) seasonsPtr = &j["months"];

    if (seasonsPtr) {
        for (const auto& sj : *seasonsPtr) {
            Season season;
            if (sj.contains("name")) {
                std::string name = sj["name"].get<std::string>();
                season.name = Utf8ToWstring(name);
            } else {
                season.name = L"Сезон";
            }

            if (sj.contains("sectors") && sj["sectors"].is_array()) {
                for (const auto& secJson : sj["sectors"]) {
                    if (!secJson.contains("type") || !secJson.contains("image")) continue;
                    Sector sector;
                    std::string typeStr = secJson["type"].get<std::string>();
                    if (typeStr == "dawn") sector.type = SectorType::Dawn;
                    else if (typeStr == "day") sector.type = SectorType::Day;
                    else if (typeStr == "dusk") sector.type = SectorType::Dusk;
                    else if (typeStr == "night") sector.type = SectorType::Night;
                    else continue;

                    std::string img = secJson["image"].get<std::string>();
                    std::replace(img.begin(), img.end(), '/', '\\');
                    sector.image = m_basePath + Utf8ToWstring(img);
                    season.sectors.push_back(sector);
                }
            }
            m_seasons.push_back(season);
        }
    }
    if (m_seasons.empty()) return false;
    return true;
}

bool Theme::extractDwwToTemp(const std::wstring& dwwPath, std::wstring& tempDir) {
    std::ifstream file(dwwPath, std::ios::binary);
    if (!file.is_open()) return false;

    char sig[4];
    file.read(sig, 4);
    if (memcmp(sig, "DWWP", 4) != 0) return false;

    int32_t version, count;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (version != 1 || count <= 0) return false;

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    tempDir = std::wstring(tempPath) + L"MDWP_" + std::to_wstring(GetTickCount()) + L"_" +
              std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(tempDir.c_str(), nullptr);

    for (int i = 0; i < count; ++i) {
        int32_t nameLen;
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        std::string fileName(nameLen, '\0');
        file.read(&fileName[0], nameLen);
        int64_t dataSize;
        file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
        std::vector<char> data(dataSize);
        if (dataSize > 0) {
            file.read(data.data(), dataSize);
        }

        std::wstring wideName = Utf8ToWstring(fileName);
        std::replace(wideName.begin(), wideName.end(), L'/', L'\\');
        fs::path destPath(tempDir + wideName);
        fs::create_directories(destPath.parent_path());

        std::ofstream out(destPath, std::ios::binary);
        if (out.is_open()) {
            out.write(data.data(), dataSize);
        }
    }

    m_basePath = tempDir;
    m_tempDir = tempDir;

    std::ifstream jsonFile(tempDir + L"theme.json");
    if (jsonFile.is_open()) {
        std::stringstream buffer;
        buffer << jsonFile.rdbuf();
        if (parseJson(buffer.str())) {
            m_loaded = true;
            return true;
        }
    }
    return false;
}

bool Theme::loadFromFile(const std::wstring& filePath) {
    m_loaded = false;
    m_seasons.clear();
    m_tempDir.clear();

    fs::path p(filePath);
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    if (ext == L".dww") {
        std::wstring tempDir;
        if (extractDwwToTemp(filePath, tempDir)) {
            return true;
        }
        return false;
    } else {
        std::ifstream file(filePath);
        if (!file.is_open()) return false;
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string jsonStr = buffer.str();

        size_t pos = filePath.find_last_of(L"\\/");
        if (pos != std::wstring::npos) m_basePath = filePath.substr(0, pos + 1);
        else m_basePath = L"";

        if (!parseJson(jsonStr)) return false;
        m_loaded = true;
        return true;
    }
}

void Theme::setLocation(double latitude, double longitude, double timezoneOffset) {
    m_latitude = latitude;
    m_longitude = longitude;
    m_timezone = timezoneOffset;
}

void Theme::setDuskAngles(double dawnAngle, double duskAngle) {
    m_dawnAngle = dawnAngle;
    m_duskAngle = duskAngle;
}

const std::wstring& Theme::getSeasonName(size_t index) const {
    static const std::wstring empty = L"";
    if (index < m_seasons.size()) return m_seasons[index].name;
    return empty;
}

int Theme::daysInMonth(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month - 1];
}

int Theme::monthFromMarch(int month) {
    if (month >= 3) return month - 2;
    return month + 10;
}

std::vector<Theme::ActiveSector> Theme::getActiveSectorsForDate(const SYSTEMTIME& now) const {
    int N = (int)m_seasons.size();
    if (N == 0) return {};

    int mfm = monthFromMarch(now.wMonth);
    int dim = daysInMonth(now.wYear, now.wMonth);
    double monthsSinceMarch = (mfm - 1) + (double)(now.wDay - 1) / dim;
    double M = 12.0 / N;
    int seasonIndex = (int)std::floor(monthsSinceMarch / M);
    if (seasonIndex >= N) seasonIndex = N - 1;
    const Season& season = m_seasons[seasonIndex];

    return buildActiveSectors(now, season);
}

std::wstring Theme::computeImage(const SYSTEMTIME& now) const {
    std::vector<ActiveSector> active = getActiveSectorsForDate(now);
    if (active.empty()) return L"";

    double currentMinutes = now.wHour * 60 + now.wMinute;

    for (const auto& as : active) {
        if (as.startMinute <= as.endMinute) {
            if (currentMinutes >= as.startMinute && currentMinutes < as.endMinute) {
                return as.image;
            }
        } else {
            // Переход через полночь
            if (currentMinutes >= as.startMinute || currentMinutes < as.endMinute) {
                return as.image;
            }
        }
    }

    return active.front().image; // fallback
}

std::wstring Theme::getImageFor(const SYSTEMTIME& now) const {
    if (!m_loaded) return L"";
    return computeImage(now);
}

std::vector<Theme::ActiveSector> Theme::buildActiveSectors(const SYSTEMTIME& now, const Season& season) const {
    std::vector<ActiveSector> result;
    if (season.sectors.empty()) return result;

    std::vector<Sector> dawnSectors, daySectors, duskSectors, nightSectors;
    for (const auto& s : season.sectors) {
        switch (s.type) {
        case SectorType::Dawn: dawnSectors.push_back(s); break;
        case SectorType::Day:  daySectors.push_back(s);  break;
        case SectorType::Dusk: duskSectors.push_back(s); break;
        case SectorType::Night: nightSectors.push_back(s); break;
        }
    }

    SolarEvents se = calculateSolarEvents(
        now.wYear, now.wMonth, now.wDay,
        m_latitude, m_longitude, m_timezone,
        m_dawnAngle, m_duskAngle
    );

    bool hasDawn = !se.polarNight && se.dawnStart >= 0;
    bool hasDusk = !se.polarNight && se.duskEnd >= 0;
    if (se.polarDay) { hasDawn = hasDusk = false; }
    if (se.polarNight) { hasDawn = hasDusk = false; }

    int totalSectors = (int)season.sectors.size();
    double normalDuration = 24.0 * 60.0 / totalSectors;
    double minDuration = normalDuration * m_minSectorFraction;

    auto addSectorsUniform = [&](const std::vector<Sector>& sectors, double start, double end, double minDur) {
        int count = (int)sectors.size();
        if (count == 0) return;
        double totalInterval;
        if (end > start) totalInterval = end - start;
        else totalInterval = (24 * 60 - start) + end;

        double durationPerSector = totalInterval / count;
        if (durationPerSector < minDur) {
            int removeCount = 0;
            while (count - removeCount * 2 > 0 && (totalInterval / (count - removeCount * 2)) < minDur) {
                removeCount++;
            }
            int finalCount = count - removeCount * 2;
            if (finalCount < 1) finalCount = 1;
            int startIndex = (count - finalCount) / 2;
            int endIndex = startIndex + finalCount;
            std::vector<Sector> kept;
            for (int i = startIndex; i < endIndex; ++i) kept.push_back(sectors[i]);
            durationPerSector = totalInterval / finalCount;
            for (size_t i = 0; i < kept.size(); ++i) {
                double curStart = start + i * durationPerSector;
                double curEnd = start + (i + 1) * durationPerSector;
                if (curStart >= 24 * 60) curStart -= 24 * 60;
                if (curEnd > 24 * 60) curEnd -= 24 * 60;
                ActiveSector as;
                as.startMinute = curStart;
                as.endMinute = curEnd;
                as.image = kept[i].image;
                as.type = kept[i].type;
                result.push_back(as);
            }
        } else {
            for (size_t i = 0; i < sectors.size(); ++i) {
                double curStart = start + i * durationPerSector;
                double curEnd = start + (i + 1) * durationPerSector;
                if (curStart >= 24 * 60) curStart -= 24 * 60;
                if (curEnd > 24 * 60) curEnd -= 24 * 60;
                ActiveSector as;
                as.startMinute = curStart;
                as.endMinute = curEnd;
                as.image = sectors[i].image;
                as.type = sectors[i].type;
                result.push_back(as);
            }
        }
    };

    if (hasDawn && !dawnSectors.empty()) {
        ActiveSector as;
        as.startMinute = se.dawnStart;
        as.endMinute = se.sunrise;
        as.image = dawnSectors.front().image;
        as.type = SectorType::Dawn;
        result.push_back(as);
    }

    if (hasDawn && hasDusk && !daySectors.empty()) {
        addSectorsUniform(daySectors, se.sunrise, se.sunset, minDuration);
    }

    if (hasDusk && !duskSectors.empty()) {
        ActiveSector as;
        as.startMinute = se.sunset;
        as.endMinute = se.duskEnd;
        as.image = duskSectors.front().image;
        as.type = SectorType::Dusk;
        result.push_back(as);
    }

    if (hasDawn && hasDusk && !nightSectors.empty()) {
        addSectorsUniform(nightSectors, se.duskEnd, se.dawnStart, minDuration);
    } else if (!hasDawn && !hasDusk) {
        if (se.polarDay && !daySectors.empty()) {
            addSectorsUniform(daySectors, 0, 24 * 60, minDuration);
        } else if (se.polarNight && !nightSectors.empty()) {
            addSectorsUniform(nightSectors, 0, 24 * 60, minDuration);
        }
    }

    return result;
}