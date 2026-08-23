#include <windows.h>
#include <string>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include "astro.h"
#include "theme.h"
#include "trayicon.h"
#include "wallpaper.h"
#include "settings.h"
#include "ui.h"
#include "resource.h"

// Глобальные переменные
Theme g_theme;
std::wstring g_themePath;
TrayIcon* g_trayIcon = nullptr;
Settings g_settings;
HINSTANCE g_hInstance = nullptr;
HANDLE g_hMutex = nullptr;

// Константы
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_SHOWMAINWINDOW = WM_APP + 2;
constexpr UINT TIMER_ID = 1;
constexpr UINT TIMER_INTERVAL_MS = 5000; // 5 секунд

std::wstring GetExePath() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) path = path.substr(0, pos);
    return path;
}

double GetSystemTimezoneOffset() {
    TIME_ZONE_INFORMATION tzi;
    GetTimeZoneInformation(&tzi);
    double bias = tzi.Bias + tzi.DaylightBias;
    return -bias / 60.0;
}

void UpdateWallpaper() {
    SYSTEMTIME now;
    GetLocalTime(&now);
    std::wstring image = g_theme.getImageFor(now);
    if (!image.empty()) {
        if (!WallpaperChanger::setWallpaper(image)) {
            MessageBoxW(nullptr, L"Не удалось установить обои", L"Ошибка", MB_ICONERROR);
        }
    }
}

bool SetAutoStart(bool enable) {
    wchar_t startupPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, startupPath))) return false;

    std::wstring shortcutPath = std::wstring(startupPath) + L"\\MyDynamicWallpaper.lnk";

    if (enable) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        IShellLinkW* pShellLink = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IShellLinkW, (void**)&pShellLink);
        if (SUCCEEDED(hr)) {
            pShellLink->SetPath(exePath);
            pShellLink->SetDescription(L"My Dynamic Wallpaper");
            pShellLink->SetWorkingDirectory(GetExePath().c_str());

            IPersistFile* pPersistFile = nullptr;
            hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
            if (SUCCEEDED(hr)) {
                hr = pPersistFile->Save(shortcutPath.c_str(), TRUE);
                pPersistFile->Release();
            }
            pShellLink->Release();
        }
        return SUCCEEDED(hr);
    } else {
        return DeleteFileW(shortcutPath.c_str()) != FALSE;
    }
}

void ChooseTheme(HWND hwnd) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"JSON темы (*.json)\0*.json\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        std::wstring newPath = file;
        if (g_theme.loadFromFile(newPath)) {
            g_themePath = newPath;
            g_settings.setThemePath(newPath);
            g_settings.save();
            UpdateWallpaper();
        } else {
            MessageBoxW(hwnd, L"Не удалось загрузить выбранную тему.", L"Ошибка", MB_ICONERROR);
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == TIMER_ID) UpdateWallpaper();
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
            g_trayIcon->showMenu(g_settings.getAutoStart());
        }
        break;

    case WM_SHOWMAINWINDOW:
        OpenMainWindow(g_hInstance, nullptr);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1: ChooseTheme(hwnd); break;
        case 2: DestroyWindow(hwnd); break;
        case 3: {
            bool newState = !g_settings.getAutoStart();
            if (SetAutoStart(newState)) {
                g_settings.setAutoStart(newState);
                g_settings.save();
            }
            break;
        }
        case 4: // Открыть интерфейс
            OpenMainWindow(g_hInstance, nullptr);
            break;
        }
        break;

    case WM_DESTROY:
        if (g_trayIcon) {
            g_trayIcon->hide();
            delete g_trayIcon;
            g_trayIcon = nullptr;
        }
        PostQuitMessage(0);
        break;

    case WM_TIMECHANGE:
        UpdateWallpaper();
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

bool CreateStartMenuShortcut() {
    wchar_t programsPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, programsPath))) {
        return false;
    }

    std::wstring shortcutPath = std::wstring(programsPath) + L"\\MyDynamicWallpaper.lnk";

    // Если ярлык уже есть, не пересоздаём
    if (GetFileAttributesW(shortcutPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* pShellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, (void**)&pShellLink);
    if (SUCCEEDED(hr)) {
        pShellLink->SetPath(exePath);
        pShellLink->SetDescription(L"My Dynamic Wallpaper");
        pShellLink->SetWorkingDirectory(GetExePath().c_str());
        pShellLink->SetIconLocation(exePath, 0); // используем нашу иконку из exe

        IPersistFile* pPersistFile = nullptr;
        hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
        if (SUCCEEDED(hr)) {
            hr = pPersistFile->Save(shortcutPath.c_str(), TRUE);
            pPersistFile->Release();
        }
        pShellLink->Release();
    }
    CoUninitialize();
    return SUCCEEDED(hr);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Проверка на повторный запуск
    g_hMutex = CreateMutexW(nullptr, TRUE, L"Global\\MyDynamicWallpaper_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Найти существующее скрытое окно
        HWND hExisting = FindWindowW(L"DynamicWallpaperClass", nullptr);
        if (hExisting) {
            PostMessageW(hExisting, WM_SHOWMAINWINDOW, 0, 0);
        }
        CloseHandle(g_hMutex);
        g_hMutex = nullptr;
        return 0;
    }

    g_hInstance = hInstance;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_settings.load();
    // Создаём ярлык в меню Пуск, если его нет
    CreateStartMenuShortcut();

    if (!g_settings.isLocationConfigured()) {
        double lat = g_settings.getLatitude();
        double lon = g_settings.getLongitude();
        if (ShowLocationDialog(hInstance, nullptr, lat, lon)) {
            g_settings.setLatitude(lat);
            g_settings.setLongitude(lon);
            g_settings.setLocationConfigured(true);
            g_settings.save();
        }
    }

    g_theme.setLocation(g_settings.getLatitude(), g_settings.getLongitude(), GetSystemTimezoneOffset());

    std::wstring savedTheme = g_settings.getThemePath();
    std::wstring exeDir = GetExePath();
    std::wstring defaultTheme = exeDir + L"\\themes\\example_theme.json";

    if (!savedTheme.empty()) {
        DWORD attr = GetFileAttributesW(savedTheme.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            g_themePath = savedTheme;
        } else {
            g_themePath = defaultTheme;
        }
    } else {
        g_themePath = defaultTheme;
    }

    if (!g_theme.loadFromFile(g_themePath)) {
        // Тема не загружена, программа продолжит работать
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DynamicWallpaperClass";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"DynamicWallpaper", 0,
                              0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (hIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
    if (!hwnd) {
        CoUninitialize();
        CloseHandle(g_hMutex);
        return 1;
    }

    g_trayIcon = new TrayIcon(hwnd, WM_TRAYICON);
    if (!g_trayIcon->show()) {
        MessageBoxW(hwnd, L"Не удалось создать иконку в трее.", L"Ошибка", MB_ICONERROR);
        delete g_trayIcon;
        DestroyWindow(hwnd);
        CoUninitialize();
        CloseHandle(g_hMutex);
        return 1;
    }

    SetTimer(hwnd, TIMER_ID, TIMER_INTERVAL_MS, nullptr);
    UpdateWallpaper();

    // Открываем интерфейс при старте (без родителя, чтобы была иконка на панели задач)
    OpenMainWindow(g_hInstance, nullptr);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    if (g_hMutex) {
        ReleaseMutex(g_hMutex);
        CloseHandle(g_hMutex);
    }
    return 0;
}