#pragma once

#include <cmath>
#include <algorithm>

// Структура с результатами расчёта солнечных событий (в минутах от местной полуночи)
struct SolarEvents {
    double sunrise = -1;         // восход (в минутах)
    double sunset = -1;          // закат (в минутах)
    double dawnStart = -1;       // начало рассвета (угол dawnAngle)
    double duskEnd = -1;         // конец заката (угол duskAngle)
    bool polarDay = false;       // солнце не заходит
    bool polarNight = false;     // солнце не восходит
};

// Вычисляет солнечные события для указанной даты и координат.
// date: год, месяц, день
// lat, lon: широта и долгота в градусах (северная/восточная положительные)
// timezoneOffset: смещение от UTC в часах (например, для Москвы зимой +3)
// dawnAngle: угол солнца для начала рассвета (по умолчанию -12°)
// duskAngle: угол солнца для конца заката (по умолчанию -12°)
SolarEvents calculateSolarEvents(int year, int month, int day,
                                 double lat, double lon,
                                 double timezoneOffset,
                                 double dawnAngle = -12.0,
                                 double duskAngle = -12.0);