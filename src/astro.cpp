#include "astro.h"
#include <numbers> // для std::numbers::pi (C++20) или определим свою константу

namespace {
    constexpr double PI = 3.14159265358979323846;
    constexpr double RAD = PI / 180.0;
    constexpr double DEG = 180.0 / PI;

    // Преобразование даты в юлианский день (JD)
    double toJulianDay(int year, int month, int day) {
        if (month <= 2) {
            year -= 1;
            month += 12;
        }
        int A = year / 100;
        int B = 2 - A + A / 4;
        return floor(365.25 * (year + 4716)) + floor(30.6001 * (month + 1)) + day + B - 1524.5;
    }

    // Вычисление солнечных координат (экваториальных) на заданный юлианский день
    void solarPosition(double jd, double& declination, double& rightAscension) {
        double T = (jd - 2451545.0) / 36525.0;
        double L0 = 280.46646 + 36000.76983 * T + 0.0003032 * T * T;
        double M = 357.52911 + 35999.05029 * T - 0.0001537 * T * T;
        double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * sin(M * RAD)
                 + (0.019993 - 0.000101 * T) * sin(2 * M * RAD)
                 + 0.000289 * sin(3 * M * RAD);
        double trueLong = L0 + C;
        double omega = 125.04 - 1934.136 * T;
        double lambda = trueLong - 0.00569 - 0.00478 * sin(omega * RAD);
        double eps = 23.439291 - 0.0130042 * T;
        declination = asin(sin(eps * RAD) * sin(lambda * RAD)) * DEG;
        double y = sin(lambda * RAD) * cos(eps * RAD);
        double x = cos(lambda * RAD);
        rightAscension = atan2(y, x) * DEG;
        if (rightAscension < 0) rightAscension += 360.0;
    }

    // Вычисление часового угла для заданного угла возвышения солнца
    double hourAngle(double lat, double declination, double elevation) {
        double latRad = lat * RAD;
        double decRad = declination * RAD;
        double cosH = (sin(elevation * RAD) - sin(latRad) * sin(decRad)) / (cos(latRad) * cos(decRad));
        if (cosH > 1.0) return -1; // солнце не поднимается до заданной высоты
        if (cosH < -1.0) return -2; // солнце не опускается ниже заданной высоты
        return acos(cosH) * DEG / 15.0; // в часах
    }
}

SolarEvents calculateSolarEvents(int year, int month, int day,
                                 double lat, double lon,
                                 double timezoneOffset,
                                 double dawnAngle,
                                 double duskAngle) {
    SolarEvents events;
    // Юлианская дата для местной полуночи (приблизительно)
    double jd = toJulianDay(year, month, day) - timezoneOffset / 24.0;
    double declination, rightAscension;
    solarPosition(jd, declination, rightAscension);

    // Солнечный полдень (в часах от местной полуночи)
    double solarNoon = 12.0 - lon / 15.0 + timezoneOffset;

    // Восход и закат для стандартного угла -0.833°
    double haSunrise = hourAngle(lat, declination, -0.833);
    double haSunset = haSunrise;
    if (haSunrise >= 0) {
        events.sunrise = (solarNoon - haSunrise) * 60.0;
        events.sunset = (solarNoon + haSunrise) * 60.0;
    } else if (haSunrise == -1) {
        events.polarNight = true; // солнце не восходит
    } else if (haSunrise == -2) {
        events.polarDay = true;   // солнце не заходит
    }

    // Начало рассвета (dawnAngle) и конец заката (duskAngle)
    double haDawn = hourAngle(lat, declination, dawnAngle);
    if (haDawn >= 0) {
        events.dawnStart = (solarNoon - haDawn) * 60.0;
        events.duskEnd = (solarNoon + haDawn) * 60.0;
    } else if (haDawn == -1) {
        // Если для -12° солнце не поднимается, значит полярная ночь для этого угла
        // Можно оставить флаги как есть
    } else if (haDawn == -2) {
        // Солнце всегда выше -12°, значит белые ночи, но не полярный день
    }

    // Корректировка значений, чтобы они были в диапазоне 0..1440 минут
    auto normalize = [](double& val) {
        while (val < 0) val += 1440.0;
        while (val >= 1440.0) val -= 1440.0;
    };
    normalize(events.sunrise);
    normalize(events.sunset);
    normalize(events.dawnStart);
    normalize(events.duskEnd);

    return events;
}