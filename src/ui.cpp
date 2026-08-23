#include "ui.h"
#include "resource.h"
#include <windows.h>
#include <gdiplus.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <uxtheme.h>
#include <nlohmann/json.hpp>

#include "theme.h"
#include "settings.h"
#include "wallpaper.h"
#include "astro.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace fs = std::filesystem;
using namespace Gdiplus;

extern "C" BOOL WINAPI GradientFill(HDC hdc, PTRIVERTEX pVertex, ULONG nVertex, PVOID pMesh, ULONG nCount, ULONG ulMode);
extern double GetSystemTimezoneOffset();
extern Theme g_theme;
extern Settings g_settings;

static HWND g_hMainDlg = nullptr;
static std::wstring g_selectedThemePath;
static int g_currentSeason = 0;
static int g_currentSector = 0;
static std::vector<std::wstring> g_themeFilePaths;
static std::vector<Theme::ActiveSector> g_activeSectors;

INT_PTR CALLBACK MainDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK LocationDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

std::wstring GetExeDir();
void LoadThemeList(HWND hwndList);
void ShowPreview(HWND hwndPreview, const std::wstring& imagePath);
void DrawBackground(HDC hdc, const RECT& rc);
void ApplyDarkTitleBar(HWND hwnd);
void DrawOwnerButton(DRAWITEMSTRUCT* dis);
void UpdateAstroInfo(HWND hwndDlg);
void UpdateUIForTheme(HWND hwndDlg);
void UpdateSectorInterval(HWND hwndDlg);
void FillSectorComboFromActive(HWND hwndDlg);

// ==== Вспомогательные функции для полярных периодов ====
void GetMonthDayFromDayOfYear(int year, int dayOfYear, int& month, int& day) {
    static int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    daysInMonth[1] = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
    int remaining = dayOfYear;
    for (int i = 0; i < 12; ++i) {
        if (remaining <= daysInMonth[i]) {
            month = i + 1;
            day = remaining;
            return;
        }
        remaining -= daysInMonth[i];
    }
    month = 12;
    day = 31;
}

void FindPolarPeriods(double lat, double lon, double tz, int year,
                      std::wstring& polarDayInfo, std::wstring& polarNightInfo) {
    bool prevDay = false, prevNight = false;
    bool hasDay = false, hasNight = false;
    int dayStart = -1, dayEnd = -1;
    int nightStart = -1, nightEnd = -1;

    for (int doy = 1; doy <= 365; ++doy) {
        int month, day;
        GetMonthDayFromDayOfYear(year, doy, month, day);
        SolarEvents se = calculateSolarEvents(year, month, day, lat, lon, tz);
        bool curDay = se.polarDay;
        bool curNight = se.polarNight;

        if (curDay && !prevDay) { dayStart = doy; hasDay = true; }
        if (!curDay && prevDay) { dayEnd = doy - 1; }
        if (curNight && !prevNight) { nightStart = doy; hasNight = true; }
        if (!curNight && prevNight) { nightEnd = doy - 1; }

        prevDay = curDay;
        prevNight = curNight;
    }
    if (prevDay && dayEnd == -1) dayEnd = 365;
    if (prevNight && nightEnd == -1) nightEnd = 365;

    auto formatDate = [&](int doy) -> std::wstring {
        int m, d;
        GetMonthDayFromDayOfYear(year, doy, m, d);
        wchar_t buf[32];
        swprintf_s(buf, L"%02d.%02d", d, m);
        return buf;
    };

    polarDayInfo = hasDay ? L"Полярный день: с " + formatDate(dayStart) + L" по " + formatDate(dayEnd)
                          : L"Полярный день отсутствует";
    polarNightInfo = hasNight ? L"Полярная ночь: с " + formatDate(nightStart) + L" по " + formatDate(nightEnd)
                              : L"Полярная ночь отсутствует";
}

// ==== Основные функции ====
std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) path = path.substr(0, pos);
    return path;
}

std::wstring Utf8ToWstring(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], len);
    result.resize(len - 1);
    return result;
}

void LoadThemeList(HWND hwndList) {
    SendMessageW(hwndList, LB_RESETCONTENT, 0, 0);
    g_themeFilePaths.clear();

    std::wstring themesDir = GetExeDir() + L"\\themes";
    if (!fs::exists(themesDir) || !fs::is_directory(themesDir)) return;

    int index = 0;
    for (const auto& entry : fs::recursive_directory_iterator(themesDir)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".dww") {
                std::wstring fullPath = entry.path().wstring();
                std::wstring displayName = entry.path().filename().wstring();
                g_themeFilePaths.push_back(fullPath);
                int lbIndex = SendMessageW(hwndList, LB_ADDSTRING, 0, (LPARAM)displayName.c_str());
                SendMessageW(hwndList, LB_SETITEMDATA, lbIndex, (LPARAM)index);
                index++;
            }
        }
    }
}

void ShowPreview(HWND hwndPreview, const std::wstring& imagePath) {
    HBITMAP hOldBmp = (HBITMAP)SendMessageW(hwndPreview, STM_GETIMAGE, IMAGE_BITMAP, 0);
    if (hOldBmp) DeleteObject(hOldBmp);

    GdiplusStartupInput gsi;
    ULONG_PTR token;
    GdiplusStartup(&token, &gsi, nullptr);

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(imagePath.c_str());
    if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
        HBITMAP hBitmap;
        bmp->GetHBITMAP(Gdiplus::Color(255,255,255), &hBitmap);
        SendMessageW(hwndPreview, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBitmap);
        delete bmp;
    } else {
        SendMessageW(hwndPreview, STM_SETIMAGE, IMAGE_BITMAP, 0);
    }
    GdiplusShutdown(token);
}

void DrawBackground(HDC hdc, const RECT& rc) {
    HBRUSH hBrush = CreateSolidBrush(RGB(45, 45, 48));
    FillRect(hdc, &rc, hBrush);
    DeleteObject(hBrush);
}

void ApplyDarkTitleBar(HWND hwnd) {
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (!hDwm) return;
    auto pDwmSetWindowAttribute = (HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD))GetProcAddress(hDwm, "DwmSetWindowAttribute");
    if (pDwmSetWindowAttribute) {
        BOOL useDark = TRUE;
        pDwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
        pDwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));
    }
    FreeLibrary(hDwm);
}

void DrawOwnerButton(DRAWITEMSTRUCT* dis) {
    if (dis->CtlType != ODT_BUTTON) return;

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    UINT state = dis->itemState;
    bool pressed = (state & ODS_SELECTED) != 0;
    bool focused = (state & ODS_FOCUS) != 0;

    HBRUSH hBg = CreateSolidBrush(RGB(45, 45, 48));
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(120,120,120));
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hPen);

    wchar_t text[128];
    GetWindowTextW(dis->hwndItem, text, 128);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, pressed ? RGB(180,180,180) : RGB(255,255,255));
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (focused) DrawFocusRect(hdc, &rc);
}

void UpdateAstroInfo(HWND hwndDlg) {
    SYSTEMTIME now;
    GetLocalTime(&now);

    SolarEvents se = calculateSolarEvents(now.wYear, now.wMonth, now.wDay,
                                          g_settings.getLatitude(),
                                          g_settings.getLongitude(),
                                          GetSystemTimezoneOffset());

    wchar_t dawnBuf[128] = L"";
    wchar_t duskBuf[128] = L"";
    wchar_t polarBuf[256] = L"";

    if (se.polarDay || se.polarNight) {
        // Времена не выводим
    } else {
        auto fmt = [](wchar_t* out, size_t size, double minutes) {
            int h = (int)(minutes / 60);
            int m = (int)minutes % 60;
            swprintf_s(out, size, L"%02d:%02d", h, m);
        };
        wchar_t t1[64], t2[64];
        fmt(t1, 64, se.dawnStart);
        fmt(t2, 64, se.sunrise);
        swprintf_s(dawnBuf, L"%s – %s", t1, t2);

        fmt(t1, 64, se.sunset);
        fmt(t2, 64, se.duskEnd);
        swprintf_s(duskBuf, L"%s – %s", t1, t2);
    }

    std::wstring polarDayInfo, polarNightInfo;
    FindPolarPeriods(g_settings.getLatitude(), g_settings.getLongitude(),
                     GetSystemTimezoneOffset(), now.wYear, polarDayInfo, polarNightInfo);

    if (!polarDayInfo.empty() && !polarNightInfo.empty()) {
        swprintf_s(polarBuf, L"%s; %s", polarDayInfo.c_str(), polarNightInfo.c_str());
    } else if (!polarDayInfo.empty()) {
        wcscpy_s(polarBuf, polarDayInfo.c_str());
    } else if (!polarNightInfo.empty()) {
        wcscpy_s(polarBuf, polarNightInfo.c_str());
    } else {
        wcscpy_s(polarBuf, L"Полярные периоды отсутствуют");
    }

    SetDlgItemTextW(hwndDlg, IDC_DAWN_INFO, dawnBuf);
    SetDlgItemTextW(hwndDlg, IDC_DUSK_INFO, duskBuf);
    SetDlgItemTextW(hwndDlg, IDC_POLAR_INFO, polarBuf);
}

void UpdateSectorInterval(HWND hwndDlg) {
    HWND hInterval = GetDlgItem(hwndDlg, IDC_SECTOR_INTERVAL);
    if (g_activeSectors.empty()) {
        SetWindowTextW(hInterval, L"");
        InvalidateRect(hInterval, nullptr, TRUE);
        return;
    }
    if (g_currentSector < 0 || g_currentSector >= (int)g_activeSectors.size()) {
        SetWindowTextW(hInterval, L"");
        InvalidateRect(hInterval, nullptr, TRUE);
        return;
    }
    const auto& sec = g_activeSectors[g_currentSector];
    auto fmtTime = [](double minutes) -> std::wstring {
        int h = (int)(minutes / 60);
        int m = (int)minutes % 60;
        wchar_t buf[16];
        swprintf_s(buf, L"%02d:%02d", h, m);
        return buf;
    };
    std::wstring interval = fmtTime(sec.startMinute) + L" – " + fmtTime(sec.endMinute);
    SetWindowTextW(hInterval, interval.c_str());
    InvalidateRect(hInterval, nullptr, TRUE);
}

void FillSectorComboFromActive(HWND hwndDlg) {
    HWND hSectorCombo = GetDlgItem(hwndDlg, IDC_SECTOR_COMBO);
    SendMessageW(hSectorCombo, CB_RESETCONTENT, 0, 0);
    for (const auto& sec : g_activeSectors) {
        std::wstring typeName;
        switch (sec.type) {
        case SectorType::Dawn: typeName = L"Рассвет"; break;
        case SectorType::Day:  typeName = L"День"; break;
        case SectorType::Dusk: typeName = L"Закат"; break;
        case SectorType::Night: typeName = L"Ночь"; break;
        default: typeName = L"Сектор"; break;
        }
        SendMessageW(hSectorCombo, CB_ADDSTRING, 0, (LPARAM)typeName.c_str());
    }
    SendMessageW(hSectorCombo, CB_SETCURSEL, 0, 0);
    g_currentSector = 0;
}

void UpdateUIForTheme(HWND hwndDlg) {
    HWND hList = GetDlgItem(hwndDlg, IDC_THEME_LIST);
    int sel = SendMessageW(hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;

    int itemData = (int)SendMessageW(hList, LB_GETITEMDATA, sel, 0);
    if (itemData < 0 || itemData >= (int)g_themeFilePaths.size()) return;
    g_selectedThemePath = g_themeFilePaths[itemData];

    if (!g_theme.loadFromFile(g_selectedThemePath)) {
        MessageBoxW(hwndDlg, L"Не удалось загрузить тему.", L"Ошибка", MB_ICONERROR);
        return;
    }

    HWND hSeasonCombo = GetDlgItem(hwndDlg, IDC_SEASON_COMBO);
    SendMessageW(hSeasonCombo, CB_RESETCONTENT, 0, 0);
    size_t seasonCount = g_theme.getSeasonCount();
    for (size_t i = 0; i < seasonCount; ++i) {
        std::wstring label = g_theme.getSeasonName(i);
        if (label.empty()) label = L"Сезон " + std::to_wstring(i + 1);
        SendMessageW(hSeasonCombo, CB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
    SendMessageW(hSeasonCombo, CB_SETCURSEL, 0, 0);
    g_currentSeason = 0;

    SYSTEMTIME now;
    GetLocalTime(&now);
    g_activeSectors = g_theme.getActiveSectorsForDate(now);
    FillSectorComboFromActive(hwndDlg);

    if (!g_activeSectors.empty()) {
        ShowPreview(GetDlgItem(hwndDlg, IDC_PREVIEW), g_activeSectors[0].image);
    }
    UpdateSectorInterval(hwndDlg);
    UpdateAstroInfo(hwndDlg);
}

// ==== Города ====
struct City {
    std::wstring name;
    double lat;
    double lon;
};

std::vector<City> LoadCities() {
    std::vector<City> cities;
    std::wstring citiesPath = GetExeDir() + L"\\cities.json";
    std::ifstream file(citiesPath);
    if (!file.is_open()) return cities;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonStr = buffer.str();

    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        if (j.is_array()) {
            for (const auto& item : j) {
                City city;
                if (item.contains("name")) {
                    std::string name = item["name"].get<std::string>();
                    city.name = Utf8ToWstring(name);
                }
                if (item.contains("lat")) city.lat = item["lat"].get<double>();
                if (item.contains("lon")) city.lon = item["lon"].get<double>();
                cities.push_back(city);
            }
        }
    } catch (...) {}
    return cities;
}

void FillCityCombo(HWND hwndDlg, int comboId) {
    HWND hCombo = GetDlgItem(hwndDlg, comboId);
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    std::vector<City> cities = LoadCities();
    for (size_t i = 0; i < cities.size(); ++i) {
        int idx = SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)cities[i].name.c_str());
        SendMessageW(hCombo, CB_SETITEMDATA, idx, (LPARAM)i);
    }
}

void HandleCitySelection(HWND hwndDlg, int comboId) {
    HWND hCombo = GetDlgItem(hwndDlg, comboId);
    int sel = SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return;

    std::vector<City> cities = LoadCities();
    if (sel < (int)cities.size()) {
        City& city = cities[sel];
        wchar_t buf[64];
        swprintf_s(buf, L"%.6f", city.lat);
        SetDlgItemTextW(hwndDlg, IDC_LAT_EDIT, buf);
        swprintf_s(buf, L"%.6f", city.lon);
        SetDlgItemTextW(hwndDlg, IDC_LON_EDIT, buf);
    }
}

// ==== Диалоги ====
INT_PTR CALLBACK MainDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        g_hMainDlg = hwnd;
        LoadThemeList(GetDlgItem(hwnd, IDC_THEME_LIST));
        ApplyDarkTitleBar(hwnd);
        UpdateAstroInfo(hwnd);
        return TRUE;

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        DrawBackground((HDC)wParam, rc);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        int ctrlId = GetDlgCtrlID(hCtrl);

        if (ctrlId == IDC_SECTOR_INTERVAL) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, RGB(45, 45, 48));
            SetTextColor(hdc, RGB(255, 255, 255));
            return (INT_PTR)CreateSolidBrush(RGB(45, 45, 48));
        } else {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            return (INT_PTR)GetStockObject(NULL_BRUSH);
        }
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(30,30,30));
        SetTextColor(hdc, RGB(255,255,255));
        return (INT_PTR)CreateSolidBrush(RGB(30,30,30));
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(30,30,30));
        SetTextColor(hdc, RGB(255,255,255));
        return (INT_PTR)CreateSolidBrush(RGB(30,30,30));
    }

    case WM_DRAWITEM: {
        DrawOwnerButton((DRAWITEMSTRUCT*)lParam);
        return TRUE;
    }

    case WM_TIMECHANGE: {
        UpdateAstroInfo(hwnd);
        SYSTEMTIME now;
        GetLocalTime(&now);
        g_activeSectors = g_theme.getActiveSectorsForDate(now);
        FillSectorComboFromActive(hwnd);
        if (!g_activeSectors.empty()) {
            ShowPreview(GetDlgItem(hwnd, IDC_PREVIEW), g_activeSectors[0].image);
        }
        UpdateSectorInterval(hwnd);
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_THEME_LIST:
            if (HIWORD(wParam) == LBN_SELCHANGE) UpdateUIForTheme(hwnd);
            break;
        case IDC_SEASON_COMBO:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = SendMessageW(GetDlgItem(hwnd, IDC_SEASON_COMBO), CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    g_currentSeason = sel;
                    SYSTEMTIME now;
                    GetLocalTime(&now);
                    g_activeSectors = g_theme.getActiveSectorsForDate(now);
                    FillSectorComboFromActive(hwnd);
                    if (!g_activeSectors.empty()) {
                        ShowPreview(GetDlgItem(hwnd, IDC_PREVIEW), g_activeSectors[0].image);
                    }
                    UpdateSectorInterval(hwnd);
                }
            }
            break;
        case IDC_SECTOR_COMBO:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = SendMessageW(GetDlgItem(hwnd, IDC_SECTOR_COMBO), CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    g_currentSector = sel;
                    if (sel >= 0 && sel < (int)g_activeSectors.size()) {
                        ShowPreview(GetDlgItem(hwnd, IDC_PREVIEW), g_activeSectors[sel].image);
                    }
                    UpdateSectorInterval(hwnd);
                }
            }
            break;
        case IDC_PREV_BUTTON: {
            HWND hSectorCombo = GetDlgItem(hwnd, IDC_SECTOR_COMBO);
            int count = (int)g_activeSectors.size();
            if (count > 0) {
                int cur = SendMessageW(hSectorCombo, CB_GETCURSEL, 0, 0);
                int newSel = (cur - 1 + count) % count;
                SendMessageW(hSectorCombo, CB_SETCURSEL, newSel, 0);
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SECTOR_COMBO, CBN_SELCHANGE), 0);
            }
            break;
        }
        case IDC_NEXT_BUTTON: {
            HWND hSectorCombo = GetDlgItem(hwnd, IDC_SECTOR_COMBO);
            int count = (int)g_activeSectors.size();
            if (count > 0) {
                int cur = SendMessageW(hSectorCombo, CB_GETCURSEL, 0, 0);
                int newSel = (cur + 1) % count;
                SendMessageW(hSectorCombo, CB_SETCURSEL, newSel, 0);
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SECTOR_COMBO, CBN_SELCHANGE), 0);
            }
            break;
        }
        case IDC_APPLY_BUTTON:
            if (!g_selectedThemePath.empty()) {
                g_theme.loadFromFile(g_selectedThemePath);
                g_settings.setThemePath(g_selectedThemePath);
                g_settings.save();
                SYSTEMTIME now;
                GetLocalTime(&now);
                std::wstring img = g_theme.getImageFor(now);
                if (!img.empty()) WallpaperChanger::setWallpaper(img);
            }
            break;
        case IDC_SETTINGS_BUTTON:
            ShowWindow(hwnd, SW_HIDE);
            DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS_DIALOG), hwnd, SettingsDlgProc, 0);
            ShowWindow(hwnd, SW_SHOW);
            UpdateAstroInfo(hwnd);
            SYSTEMTIME now2;
            GetLocalTime(&now2);
            g_activeSectors = g_theme.getActiveSectorsForDate(now2);
            FillSectorComboFromActive(hwnd);
            if (!g_activeSectors.empty()) {
                ShowPreview(GetDlgItem(hwnd, IDC_PREVIEW), g_activeSectors[0].image);
            }
            UpdateSectorInterval(hwnd);
            break;
        case IDCANCEL:
            DestroyWindow(hwnd);
            g_hMainDlg = nullptr;
            return TRUE;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        g_hMainDlg = nullptr;
        return TRUE;

    case WM_DESTROY:
        return 0;
    }
    return FALSE;
}

INT_PTR CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        wchar_t buf[64];
        swprintf_s(buf, L"%.6f", g_settings.getLatitude());
        SetDlgItemTextW(hwnd, IDC_LAT_EDIT, buf);
        swprintf_s(buf, L"%.6f", g_settings.getLongitude());
        SetDlgItemTextW(hwnd, IDC_LON_EDIT, buf);
        FillCityCombo(hwnd, IDC_CITY_COMBO);
        ApplyDarkTitleBar(hwnd);
        HICON hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));
        if (hIcon) {
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
        return TRUE;
    }
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        DrawBackground((HDC)wParam, rc);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255,255,255));
        return (INT_PTR)GetStockObject(NULL_BRUSH);
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(30,30,30));
        SetTextColor(hdc, RGB(255,255,255));
        return (INT_PTR)CreateSolidBrush(RGB(30,30,30));
    }
    case WM_DRAWITEM: {
        DrawOwnerButton((DRAWITEMSTRUCT*)lParam);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_CITY_COMBO && HIWORD(wParam) == CBN_SELCHANGE) {
            HandleCitySelection(hwnd, IDC_CITY_COMBO);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SAVE_BUTTON) {
            wchar_t latBuf[64], lonBuf[64];
            GetDlgItemTextW(hwnd, IDC_LAT_EDIT, latBuf, 64);
            GetDlgItemTextW(hwnd, IDC_LON_EDIT, lonBuf, 64);
            try {
                double lat = std::stod(latBuf);
                double lon = std::stod(lonBuf);
                g_settings.setLatitude(lat);
                g_settings.setLongitude(lon);
                g_settings.setLocationConfigured(true);
                g_settings.save();
                g_theme.setLocation(lat, lon, GetSystemTimezoneOffset());
                EndDialog(hwnd, IDOK);
            } catch (...) {
                MessageBoxW(hwnd, L"Некорректные числа", L"Ошибка", MB_ICONERROR);
            }
            return TRUE;
        } else if (LOWORD(wParam) == IDC_BACK_BUTTON) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK LocationDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static double* pLat = nullptr;
    static double* pLon = nullptr;
    switch (msg) {
    case WM_INITDIALOG: {
        auto* params = reinterpret_cast<std::pair<double*, double*>*>(lParam);
        pLat = params->first;
        pLon = params->second;
        FillCityCombo(hwnd, IDC_CITY_COMBO);
        ApplyDarkTitleBar(hwnd);
        HICON hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));
        if (hIcon) {
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
        return TRUE;
    }
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        DrawBackground((HDC)wParam, rc);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255,255,255));
        return (INT_PTR)GetStockObject(NULL_BRUSH);
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(30,30,30));
        SetTextColor(hdc, RGB(255,255,255));
        return (INT_PTR)CreateSolidBrush(RGB(30,30,30));
    }
    case WM_DRAWITEM: {
        DrawOwnerButton((DRAWITEMSTRUCT*)lParam);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_CITY_COMBO && HIWORD(wParam) == CBN_SELCHANGE) {
            HandleCitySelection(hwnd, IDC_CITY_COMBO);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SAVE_BUTTON) {
            wchar_t latBuf[64], lonBuf[64];
            GetDlgItemTextW(hwnd, IDC_LAT_EDIT, latBuf, 64);
            GetDlgItemTextW(hwnd, IDC_LON_EDIT, lonBuf, 64);
            try {
                *pLat = std::stod(latBuf);
                *pLon = std::stod(lonBuf);
                EndDialog(hwnd, IDOK);
            } catch (...) {
                MessageBoxW(hwnd, L"Некорректные числа", L"Ошибка", MB_ICONERROR);
            }
            return TRUE;
        } else if (LOWORD(wParam) == IDC_CANCEL_BUTTON) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// Открытие главного окна
bool OpenMainWindow(HINSTANCE hInstance, HWND parent) {
    if (g_hMainDlg) {
        SetForegroundWindow(g_hMainDlg);
        return true;
    }
    HWND hDlg = CreateDialogParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN_DIALOG), nullptr, MainDlgProc, 0);
    if (!hDlg) return false;
    HICON hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));
    if (hIcon) {
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
    return true;
}

// Показ диалога координат
bool ShowLocationDialog(HINSTANCE hInstance, HWND parent,
                        double& latitude, double& longitude) {
    auto params = std::make_pair(&latitude, &longitude);
    INT_PTR result = DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_LOCATION_DIALOG), parent,
                                     LocationDlgProc, reinterpret_cast<LPARAM>(&params));
    return (result == IDOK);
}