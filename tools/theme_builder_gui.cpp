#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;
using namespace Gdiplus;

// Глобальные элементы главного окна
HWND g_hSeasonList = nullptr;
HWND g_hFileList = nullptr;
HWND g_hStatus = nullptr;
HWND g_hThemeNameEdit = nullptr;

// Глобальные элементы редактора
HWND g_hEditDlg = nullptr;
int g_editSeasonIndex = -1;

// Структура сезона
struct Season {
    std::wstring name;
    std::vector<std::wstring> images;
    std::wstring keyDawn;
    std::wstring keyDay;
    std::wstring keyDusk;
    std::wstring keyNight;
};

std::vector<Season> g_seasons;
std::wstring g_themeName = L"MyTheme";

// Прототипы функций
void AddSeason();
void AddImagesToSelectedSeason();
void AddFolderToSelectedSeason(const std::wstring& folderPath);
void UpdateSeasonList();
void ShowSelectedSeasonImages();
HBITMAP CreateThumbnail(const std::wstring& imagePath, int maxWidth, int maxHeight);
void OpenSeasonEditor(int seasonIndex);
void ExportTheme();
void LoadThemeFromDww();
void ShowHelpWindow(HWND parent);
std::wstring GetExeDir();
std::string WStringToUtf8(const std::wstring& wstr);
std::wstring Utf8ToWstring(const std::string& utf8);
void DrawBackground(HDC hdc, const RECT& rc);
void ApplyDarkTitleBar(HWND hwnd);
void DrawOwnerButton(DRAWITEMSTRUCT* dis);
LRESULT CALLBACK HelpWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ====== Функции оформления (тёмная тема) ======
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

// ====== Вспомогательные функции ======
std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) path = path.substr(0, pos);
    return path;
}

std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
    result.resize(len - 1);
    return result;
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

HBITMAP CreateThumbnail(const std::wstring& imagePath, int maxWidth, int maxHeight) {
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromFile(imagePath.c_str());
    if (!src || src->GetLastStatus() != Gdiplus::Ok) {
        if (src) delete src;
        GdiplusShutdown(gdiplusToken);
        return nullptr;
    }

    int srcW = src->GetWidth();
    int srcH = src->GetHeight();
    double scale = min((double)maxWidth / srcW, (double)maxHeight / srcH);
    int destW = (int)(srcW * scale);
    int destH = (int)(srcH * scale);
    if (destW < 1) destW = 1;
    if (destH < 1) destH = 1;

    Gdiplus::Bitmap* thumb = new Gdiplus::Bitmap(destW, destH, PixelFormat24bppRGB);
    Gdiplus::Graphics* g = Gdiplus::Graphics::FromImage(thumb);
    g->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g->DrawImage(src, 0, 0, destW, destH);
    delete g;
    delete src;

    HBITMAP hBitmap = nullptr;
    thumb->GetHBITMAP(Gdiplus::Color(255,255,255), &hBitmap);
    delete thumb;

    GdiplusShutdown(gdiplusToken);
    return hBitmap;
}

void ClearFileList() {
    if (g_hFileList) {
        SendMessageW(g_hFileList, LVM_DELETEALLITEMS, 0, 0);
    }
}

void AddSeason() {
    Season s;
    s.name = L"Сезон " + std::to_wstring(g_seasons.size() + 1);
    g_seasons.push_back(s);
    UpdateSeasonList();
}

void AddImagesToSelectedSeason() {
    int sel = SendMessageW(g_hSeasonList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || sel >= (int)g_seasons.size()) {
        MessageBoxW(nullptr, L"Сначала выберите сезон.", L"Ошибка", MB_ICONERROR);
        return;
    }

    IFileOpenDialog* pDlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg)))) {
        DWORD flags;
        pDlg->GetOptions(&flags);
        pDlg->SetOptions(flags | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        COMDLG_FILTERSPEC rgSpec[] = {
            { L"Изображения", L"*.jpg;*.jpeg;*.png;*.bmp;*.webp" },
            { L"Все файлы", L"*.*" }
        };
        pDlg->SetFileTypes(2, rgSpec);
        pDlg->SetFileTypeIndex(1);
        pDlg->SetDefaultExtension(L"jpg");

        if (SUCCEEDED(pDlg->Show(nullptr))) {
            IShellItemArray* pItems = nullptr;
            if (SUCCEEDED(pDlg->GetResults(&pItems))) {
                DWORD count = 0;
                pItems->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pItems->GetItemAt(i, &pItem))) {
                        PWSTR pszPath = nullptr;
                        if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                            g_seasons[sel].images.push_back(pszPath);
                            CoTaskMemFree(pszPath);
                        }
                        pItem->Release();
                    }
                }
                pItems->Release();
                SetWindowTextW(g_hStatus, (L"Добавлено изображений: " + std::to_wstring(g_seasons[sel].images.size())).c_str());
            }
        }
        pDlg->Release();
    }
}

void AddFolderToSelectedSeason(const std::wstring& folderPath) {
    int sel = SendMessageW(g_hSeasonList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || sel >= (int)g_seasons.size()) {
        Season s;
        s.name = L"Сезон " + std::to_wstring(g_seasons.size() + 1);
        g_seasons.push_back(s);
        sel = (int)g_seasons.size() - 1;
        UpdateSeasonList();
        SendMessageW(g_hSeasonList, LB_SETCURSEL, sel, 0);
    }

    for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
        if (entry.is_regular_file()) {
            std::wstring ext = entry.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".jpg" || ext == L".jpeg" || ext == L".png" || ext == L".bmp" || ext == L".webp") {
                g_seasons[sel].images.push_back(entry.path().wstring());
            }
        }
    }
    SetWindowTextW(g_hStatus, (L"Добавлено изображений: " + std::to_wstring(g_seasons[sel].images.size())).c_str());
}

void UpdateSeasonList() {
    SendMessageW(g_hSeasonList, LB_RESETCONTENT, 0, 0);
    for (const auto& s : g_seasons) {
        SendMessageW(g_hSeasonList, LB_ADDSTRING, 0, (LPARAM)s.name.c_str());
    }
}

void ShowSelectedSeasonImages() {
    int sel = SendMessageW(g_hSeasonList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || sel >= (int)g_seasons.size()) return;

    ClearFileList();

    const auto& images = g_seasons[sel].images;
    for (size_t i = 0; i < images.size(); ++i) {
        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = (int)i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)fs::path(images[i]).filename().wstring().c_str();
        lvi.lParam = (LPARAM)i;
        SendMessageW(g_hFileList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
    }
}

// ==================== ОКНО РЕДАКТОРА СЕЗОНА ====================
HWND g_hEditImageList = nullptr;
HWND g_hEditPreview = nullptr;
HWND g_hRadioDawn = nullptr;
HWND g_hRadioDay = nullptr;
HWND g_hRadioDusk = nullptr;
HWND g_hRadioNight = nullptr;
HWND g_hEditName = nullptr;

void UpdateEditorImageList(int seasonIndex) {
    SendMessageW(g_hEditImageList, LB_RESETCONTENT, 0, 0);
    const auto& images = g_seasons[seasonIndex].images;
    for (const auto& img : images) {
        std::wstring fileName = fs::path(img).filename().wstring();
        SendMessageW(g_hEditImageList, LB_ADDSTRING, 0, (LPARAM)fileName.c_str());
    }
    SendMessageW(g_hEditImageList, LB_SETCURSEL, -1, 0);
    SendMessageW(g_hEditPreview, STM_SETIMAGE, IMAGE_BITMAP, 0);
}

void UpdateEditorPreview(int seasonIndex) {
    int sel = SendMessageW(g_hEditImageList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || sel >= (int)g_seasons[seasonIndex].images.size()) {
        SendMessageW(g_hEditPreview, STM_SETIMAGE, IMAGE_BITMAP, 0);
        return;
    }
    std::wstring path = g_seasons[seasonIndex].images[sel];
    HBITMAP hThumb = CreateThumbnail(path, 300, 200);
    if (hThumb) {
        SendMessageW(g_hEditPreview, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hThumb);
    } else {
        SendMessageW(g_hEditPreview, STM_SETIMAGE, IMAGE_BITMAP, 0);
    }
}

void AssignKeyRole(int seasonIndex, std::wstring Season::* keyField) {
    int sel = SendMessageW(g_hEditImageList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || sel >= (int)g_seasons[seasonIndex].images.size()) {
        MessageBoxW(g_hEditDlg, L"Сначала выберите изображение в списке.", L"Ошибка", MB_ICONERROR);
        return;
    }
    g_seasons[seasonIndex].*keyField = g_seasons[seasonIndex].images[sel];
}

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        int seasonIndex = g_editSeasonIndex;

        CreateWindowW(L"STATIC", L"Название сезона:", WS_VISIBLE | WS_CHILD, 10, 10, 120, 20, hwnd, nullptr, nullptr, nullptr);
        g_hEditName = CreateWindowW(L"EDIT", g_seasons[seasonIndex].name.c_str(),
                                    WS_VISIBLE | WS_CHILD | WS_BORDER, 130, 10, 250, 20, hwnd, nullptr, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Изображения:", WS_VISIBLE | WS_CHILD, 10, 40, 120, 20, hwnd, nullptr, nullptr, nullptr);
        g_hEditImageList = CreateWindowW(L"LISTBOX", nullptr, WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                                         10, 60, 200, 280, hwnd, (HMENU)100, nullptr, nullptr);
        g_hEditPreview = CreateWindowW(L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_BITMAP | SS_CENTERIMAGE | WS_BORDER,
                                      220, 60, 300, 200, hwnd, nullptr, nullptr, nullptr);

        CreateWindowW(L"BUTTON", L"Вверх", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 10, 350, 60, 25, hwnd, (HMENU)101, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Вниз", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 80, 350, 60, 25, hwnd, (HMENU)102, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Удалить", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 150, 350, 60, 25, hwnd, (HMENU)103, nullptr, nullptr);

        CreateWindowW(L"GROUPBOX", L"Назначить роль", WS_VISIBLE | WS_CHILD, 220, 270, 300, 100, hwnd, nullptr, nullptr, nullptr);
        g_hRadioDawn = CreateWindowW(L"BUTTON", L"Рассвет", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
                                     230, 290, 80, 20, hwnd, (HMENU)110, nullptr, nullptr);
        g_hRadioDay = CreateWindowW(L"BUTTON", L"Полдень", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
                                     230, 310, 80, 20, hwnd, (HMENU)111, nullptr, nullptr);
        g_hRadioDusk = CreateWindowW(L"BUTTON", L"Закат", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
                                     320, 290, 80, 20, hwnd, (HMENU)112, nullptr, nullptr);
        g_hRadioNight = CreateWindowW(L"BUTTON", L"Полночь", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
                                     320, 310, 80, 20, hwnd, (HMENU)113, nullptr, nullptr);

        CreateWindowW(L"BUTTON", L"Сохранить", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 10, 400, 100, 30, hwnd, (HMENU)120, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Отмена", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 120, 400, 100, 30, hwnd, (HMENU)121, nullptr, nullptr);

        UpdateEditorImageList(seasonIndex);
        ApplyDarkTitleBar(hwnd);
        break;
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        DrawBackground((HDC)wParam, rc);
        return 1;
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

    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(30,30,30));
        SetTextColor(hdc, RGB(255,255,255));
        return (INT_PTR)CreateSolidBrush(RGB(30,30,30));
    }

    case WM_DRAWITEM: {
        DrawOwnerButton((DRAWITEMSTRUCT*)lParam);
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int seasonIndex = g_editSeasonIndex;
        switch (id) {
        case 100:
            if (HIWORD(wParam) == LBN_SELCHANGE) UpdateEditorPreview(seasonIndex);
            break;
        case 101: {
            int sel = SendMessageW(g_hEditImageList, LB_GETCURSEL, 0, 0);
            if (sel > 0 && sel < (int)g_seasons[seasonIndex].images.size()) {
                std::swap(g_seasons[seasonIndex].images[sel], g_seasons[seasonIndex].images[sel-1]);
                UpdateEditorImageList(seasonIndex);
                SendMessageW(g_hEditImageList, LB_SETCURSEL, sel-1, 0);
                UpdateEditorPreview(seasonIndex);
            }
            break;
        }
        case 102: {
            int sel = SendMessageW(g_hEditImageList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_seasons[seasonIndex].images.size()-1) {
                std::swap(g_seasons[seasonIndex].images[sel], g_seasons[seasonIndex].images[sel+1]);
                UpdateEditorImageList(seasonIndex);
                SendMessageW(g_hEditImageList, LB_SETCURSEL, sel+1, 0);
                UpdateEditorPreview(seasonIndex);
            }
            break;
        }
        case 103: {
            int sel = SendMessageW(g_hEditImageList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_seasons[seasonIndex].images.size()) {
                g_seasons[seasonIndex].images.erase(g_seasons[seasonIndex].images.begin() + sel);
                UpdateEditorImageList(seasonIndex);
                UpdateEditorPreview(seasonIndex);
            }
            break;
        }
        case 110: AssignKeyRole(seasonIndex, &Season::keyDawn); break;
        case 111: AssignKeyRole(seasonIndex, &Season::keyDay); break;
        case 112: AssignKeyRole(seasonIndex, &Season::keyDusk); break;
        case 113: AssignKeyRole(seasonIndex, &Season::keyNight); break;
        case 120: {
            wchar_t nameBuf[256];
            GetWindowTextW(g_hEditName, nameBuf, 256);
            g_seasons[seasonIndex].name = nameBuf;

            bool allAssigned = !g_seasons[seasonIndex].keyDawn.empty() &&
                               !g_seasons[seasonIndex].keyDay.empty() &&
                               !g_seasons[seasonIndex].keyDusk.empty() &&
                               !g_seasons[seasonIndex].keyNight.empty();
            if (!allAssigned) {
                MessageBoxW(hwnd, L"Не все ключевые роли назначены (рассвет, полдень, закат, полночь).\nВы можете сохранить, но тема может не загрузиться.", L"Предупреждение", MB_ICONWARNING);
            }
            UpdateSeasonList();
            ShowSelectedSeasonImages();
            DestroyWindow(hwnd);
            g_hEditDlg = nullptr;
            return 0;
        }
        case 121: {
            DestroyWindow(hwnd);
            g_hEditDlg = nullptr;
            return 0;
        }
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        g_hEditDlg = nullptr;
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void OpenSeasonEditor(int seasonIndex) {
    if (g_hEditDlg) return;

    g_editSeasonIndex = seasonIndex;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = EditorWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SeasonEditorClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g_hEditDlg = CreateWindowW(L"SeasonEditorClass", L"Редактор сезона", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               200, 200, 550, 480, nullptr, nullptr, wc.hInstance, nullptr);
    if (g_hEditDlg) {
        EnableWindow(GetParent(g_hEditDlg), FALSE);
    }
}

// ================================================================
// ЗАГРУЗКА ТЕМЫ ИЗ .DWW
// ================================================================

void LoadThemeFromDww() {
    IFileOpenDialog* pDlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg)))) {
        DWORD flags;
        pDlg->GetOptions(&flags);
        pDlg->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        COMDLG_FILTERSPEC rgSpec[] = {
            { L"DWW темы", L"*.dww" },
            { L"Все файлы", L"*.*" }
        };
        pDlg->SetFileTypes(2, rgSpec);
        pDlg->SetFileTypeIndex(1);
        pDlg->SetDefaultExtension(L"dww");

        if (SUCCEEDED(pDlg->Show(nullptr))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pDlg->GetResult(&pItem))) {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                    std::wstring dwwPath = pszPath;
                    CoTaskMemFree(pszPath);

                    wchar_t tempPath[MAX_PATH];
                    GetTempPathW(MAX_PATH, tempPath);
                    std::wstring tempDir = std::wstring(tempPath) + L"MDWP_Builder_Load_" +
                                           std::to_wstring(GetTickCount()) + L"_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
                    CreateDirectoryW(tempDir.c_str(), nullptr);

                    std::ifstream file(dwwPath, std::ios::binary);
                    if (!file.is_open()) {
                        MessageBoxW(nullptr, L"Не удалось открыть файл .dww.", L"Ошибка", MB_ICONERROR);
                        return;
                    }

                    char sig[4];
                    file.read(sig, 4);
                    if (memcmp(sig, "DWWP", 4) != 0) {
                        MessageBoxW(nullptr, L"Неверный формат файла .dww.", L"Ошибка", MB_ICONERROR);
                        return;
                    }

                    int32_t version, fileCount;
                    file.read(reinterpret_cast<char*>(&version), sizeof(version));
                    file.read(reinterpret_cast<char*>(&fileCount), sizeof(fileCount));
                    if (version != 1 || fileCount <= 0) {
                        MessageBoxW(nullptr, L"Некорректный архив .dww.", L"Ошибка", MB_ICONERROR);
                        return;
                    }

                    for (int i = 0; i < fileCount; ++i) {
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
                    file.close();

                    std::ifstream jsonFile(tempDir + L"theme.json");
                    if (!jsonFile.is_open()) {
                        MessageBoxW(nullptr, L"В архиве отсутствует theme.json.", L"Ошибка", MB_ICONERROR);
                        return;
                    }
                    std::stringstream jsonBuf;
                    jsonBuf << jsonFile.rdbuf();
                    std::string jsonStr = jsonBuf.str();
                    jsonFile.close();

                    try {
                        nlohmann::json j = nlohmann::json::parse(jsonStr);
                        if (j.contains("name")) {
                            std::string name = j["name"].get<std::string>();
                            g_themeName = Utf8ToWstring(name);
                        }

                        g_seasons.clear();
                        if (j.contains("seasons") && j["seasons"].is_array()) {
                            int seasonIndex = 0;
                            for (const auto& sj : j["seasons"]) {
                                Season season;
                                seasonIndex++;
                                if (sj.contains("name")) {
                                    std::string name = sj["name"].get<std::string>();
                                    season.name = Utf8ToWstring(name);
                                }
                                if (season.name.empty()) {
                                    season.name = L"Сезон " + std::to_wstring(seasonIndex);
                                }

                                if (sj.contains("sectors") && sj["sectors"].is_array()) {
                                    for (const auto& sec : sj["sectors"]) {
                                        std::string type = sec["type"].get<std::string>();
                                        std::string imgRel = sec["image"].get<std::string>();
                                        std::wstring imgPath = tempDir + Utf8ToWstring(imgRel);
                                        season.images.push_back(imgPath);
                                        if (type == "dawn") season.keyDawn = imgPath;
                                        else if (type == "day") season.keyDay = imgPath;
                                        else if (type == "dusk") season.keyDusk = imgPath;
                                        else if (type == "night") season.keyNight = imgPath;
                                    }
                                }
                                g_seasons.push_back(season);
                            }
                        }
                    } catch (...) {
                        MessageBoxW(nullptr, L"Ошибка разбора theme.json.", L"Ошибка", MB_ICONERROR);
                        return;
                    }

                    SetWindowTextW(g_hThemeNameEdit, g_themeName.c_str());
                    UpdateSeasonList();
                    if (!g_seasons.empty()) {
                        SendMessageW(g_hSeasonList, LB_SETCURSEL, 0, 0);
                        ShowSelectedSeasonImages();
                    }
                    MessageBoxW(nullptr, L"Тема загружена успешно.", L"Готово", MB_OK);
                }
                pItem->Release();
            }
        }
        pDlg->Release();
    }
}

// ================================================================
// ЭКСПОРТ В .DWW
// ================================================================

void ExportTheme() {
    if (g_seasons.empty()) {
        MessageBoxW(nullptr, L"Нет сезонов для экспорта.", L"Ошибка", MB_ICONERROR);
        return;
    }

    for (size_t i = 0; i < g_seasons.size(); ++i) {
        const auto& s = g_seasons[i];
        if (s.keyDawn.empty() || s.keyDay.empty() || s.keyDusk.empty() || s.keyNight.empty()) {
            std::wstring msg = L"В сезоне \"" + s.name + L"\" не все ключевые роли назначены.\nТребуются: рассвет, полдень, закат, полночь.";
            MessageBoxW(nullptr, msg.c_str(), L"Ошибка экспорта", MB_ICONERROR);
            return;
        }
    }

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring tempDir = std::wstring(tempPath) + L"MDWP_Builder_Export_" +
                           std::to_wstring(GetTickCount()) + L"_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(tempDir.c_str(), nullptr);

    std::wstringstream wjson;
    wjson << L"{\n";
    wjson << L"  \"name\": \"" << g_themeName << L"\",\n";
    wjson << L"  \"description\": \"Created with ThemeBuilder\",\n";
    wjson << L"  \"sunElevationDawn\": -12.0,\n";
    wjson << L"  \"sunElevationDusk\": -12.0,\n";
    wjson << L"  \"minSectorFraction\": 0.25,\n";
    wjson << L"  \"seasons\": [\n";

    for (size_t i = 0; i < g_seasons.size(); ++i) {
        const Season& season = g_seasons[i];
        std::wstring seasonFolder = L"season_" + std::to_wstring(i + 1);
        fs::create_directory(tempDir + seasonFolder);

        wjson << L"    {\n";
        wjson << L"      \"name\": \"" << season.name << L"\",\n";
        wjson << L"      \"sectors\": [\n";

        struct SectorInfo {
            std::wstring type;
            std::wstring relPath;
        };
        std::vector<SectorInfo> sectors;

        int idxDawn = -1, idxDay = -1, idxDusk = -1, idxNight = -1;
        for (size_t j = 0; j < season.images.size(); ++j) {
            if (season.images[j] == season.keyDawn) idxDawn = (int)j;
            if (season.images[j] == season.keyDay) idxDay = (int)j;
            if (season.images[j] == season.keyDusk) idxDusk = (int)j;
            if (season.images[j] == season.keyNight) idxNight = (int)j;
        }

        for (size_t j = 0; j < season.images.size(); ++j) {
            fs::path src(season.images[j]);
            std::wstring ext = src.extension().wstring();
            std::wstring newFileName = L"img_" + std::to_wstring(j) + ext;
            fs::path dest = fs::path(tempDir + seasonFolder) / newFileName;
            try {
                fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
            } catch (...) {
                std::wstring errMsg = L"Ошибка копирования файла: " + season.images[j];
                MessageBoxW(nullptr, errMsg.c_str(), L"Ошибка", MB_ICONERROR);
                fs::remove_all(tempDir);
                return;
            }
            std::wstring relPath = seasonFolder + L"/" + newFileName;

            std::wstring type;
            if ((int)j == idxDawn) type = L"dawn";
            else if ((int)j == idxDay) type = L"day";
            else if ((int)j == idxDusk) type = L"dusk";
            else if ((int)j == idxNight) type = L"night";
            else {
                if (idxNight >= 0 && idxDawn >= 0 && (int)j > idxNight && (int)j < idxDawn) type = L"night";
                else if (idxDawn >= 0 && idxDay >= 0 && (int)j > idxDawn && (int)j < idxDay) type = L"day";
                else if (idxDay >= 0 && idxDusk >= 0 && (int)j > idxDay && (int)j < idxDusk) type = L"day";
                else if (idxDusk >= 0 && idxNight >= 0 && (int)j > idxDusk && (int)j < idxNight) type = L"night";
                else type = L"night";
            }
            sectors.push_back({type, relPath});
        }

        for (size_t k = 0; k < sectors.size(); ++k) {
            wjson << L"        {\n";
            wjson << L"          \"image\": \"" << sectors[k].relPath << L"\",\n";
            wjson << L"          \"type\": \"" << sectors[k].type << L"\"\n";
            wjson << L"        }" << (k == sectors.size() - 1 ? L"\n" : L",\n");
        }

        wjson << L"      ]\n";
        wjson << L"    }" << (i == g_seasons.size() - 1 ? L"\n" : L",\n");
    }

    wjson << L"  ]\n";
    wjson << L"}\n";

    std::string jsonStr = WStringToUtf8(wjson.str());

    std::ofstream jsonFile(tempDir + L"theme.json", std::ios::binary);
    if (!jsonFile.is_open()) {
        fs::remove_all(tempDir);
        MessageBoxW(nullptr, L"Не удалось создать theme.json.", L"Ошибка", MB_ICONERROR);
        return;
    }
    jsonFile.write(jsonStr.data(), jsonStr.size());
    jsonFile.close();

    std::wstring outDww = tempDir + L"theme.dww";
    std::ofstream dww(outDww, std::ios::binary);
    if (!dww.is_open()) {
        fs::remove_all(tempDir);
        MessageBoxW(nullptr, L"Не удалось создать .dww файл.", L"Ошибка", MB_ICONERROR);
        return;
    }

    dww.write("DWWP", 4);
    int32_t version = 1;
    dww.write(reinterpret_cast<const char*>(&version), sizeof(version));

    std::vector<fs::path> allFiles;
    for (const auto& entry : fs::recursive_directory_iterator(tempDir)) {
        if (entry.is_regular_file()) {
            allFiles.push_back(entry.path());
        }
    }

    int32_t fileCount = (int32_t)allFiles.size();
    dww.write(reinterpret_cast<const char*>(&fileCount), sizeof(fileCount));

    for (const auto& filePath : allFiles) {
        fs::path relPath = filePath.lexically_relative(tempDir);
        std::wstring relW = relPath.generic_wstring();
        std::string relUtf8 = WStringToUtf8(relW);

        int32_t nameLen = (int32_t)relUtf8.size();
        dww.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        dww.write(relUtf8.data(), nameLen);

        std::ifstream in(filePath, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        int64_t dataSize = (int64_t)data.size();
        dww.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
        if (dataSize > 0) dww.write(data.data(), dataSize);
        in.close();
    }
    dww.close();

    std::wstring themesDir = GetExeDir() + L"\\themes";
    if (!fs::exists(themesDir)) fs::create_directory(themesDir);
    fs::path destDww = themesDir + L"\\" + g_themeName + L".dww";
    try {
        fs::copy_file(outDww, destDww, fs::copy_options::overwrite_existing);
    } catch (...) {
        fs::remove_all(tempDir);
        MessageBoxW(nullptr, L"Не удалось скопировать .dww в папку themes.", L"Ошибка", MB_ICONERROR);
        return;
    }

    fs::remove_all(tempDir);

    std::wstring successMsg = L"Тема успешно экспортирована:\n" + destDww.wstring();
    MessageBoxW(nullptr, successMsg.c_str(), L"Готово", MB_OK);
}

// ================================================================
// СПРАВКА
// ================================================================
void ShowHelpWindow(HWND parent) {
    // Инструкция
    const wchar_t* helpText = L"Инструкция по созданию темы\r\n\r\n"
        L"1. Задайте название темы в поле 'Название темы'.\r\n"
        L"2. Кнопкой 'Добавить сезон' создайте необходимое количество сезонов (минимум 4, но можно больше).\r\n"
        L"   Каждый сезон - это интервал года, начиная с весны (1 марта).\r\n"
        L"3. Выберите сезон в левом списке и добавьте изображения:\r\n"
        L"   - нажмите 'Добавить изображения' для выбора файлов;\r\n"
        L"   - или перетащите файлы/папки из проводника в окно.\r\n"
        L"4. Дважды щёлкните по сезону, чтобы открыть редактор.\r\n"
        L"   В редакторе можно:\r\n"
        L"   - менять порядок изображений кнопками 'Вверх'/'Вниз';\r\n"
        L"   - удалять изображения;\r\n"
        L"   - назначить ключевые роли: выберите изображение в списке и нажмите одну из радиокнопок (Рассвет, Полдень, Закат, Полночь).\r\n"
        L"   Без назначения всех четырёх ролей тема не будет загружаться в основную программу.\r\n"
        L"5. После настройки всех сезонов нажмите 'Создать .dww'.\r\n"
        L"   Тема будет упакована в файл .dww и автоматически скопирована в папку themes рядом с основной программой.\r\n"
        L"6. Для редактирования готовой темы используйте кнопку 'Открыть тему'.\r\n\r\n"
        L"Советы:\r\n"
        L"- Минимальное количество изображений в сезоне: 4 (рассвет, полдень, закат, полночь).\r\n"
        L"- Промежуточные изображения можно добавлять между ключевыми, порядок их следования задаётся в редакторе.\r\n"
        L"- Чем больше сезонов, тем более плавно тема будет меняться в течение года.\r\n";

    // Создаём окно справки
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = HelpWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"HelpWindowClass";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }

    HWND hHelpWnd = CreateWindowW(L"HelpWindowClass", L"Справка", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  300, 300, 600, 500, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hHelpWnd) return;

    // Создаём многострочный EDIT с текстом
    HWND hEdit = CreateWindowW(L"EDIT", helpText,
                               WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                               10, 10, 560, 400, hHelpWnd, (HMENU)1000, nullptr, nullptr);
    // Устанавливаем шрифт по умолчанию
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    // Кнопка "Закрыть"
    CreateWindowW(L"BUTTON", L"Закрыть", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                  250, 420, 100, 30, hHelpWnd, (HMENU)1001, nullptr, nullptr);

    ApplyDarkTitleBar(hHelpWnd);
    EnableWindow(parent, FALSE);
}

LRESULT CALLBACK HelpWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        DrawBackground((HDC)wParam, rc);
        return 1;
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
        if (LOWORD(wParam) == 1001) { // Закрыть
            EnableWindow(GetParent(hwnd), TRUE);
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        EnableWindow(GetParent(hwnd), TRUE);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"Название темы:", WS_VISIBLE | WS_CHILD, 10, 10, 100, 20, hwnd, nullptr, nullptr, nullptr);
        g_hThemeNameEdit = CreateWindowW(L"EDIT", g_themeName.c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER,
                                         110, 10, 200, 20, hwnd, (HMENU)200, nullptr, nullptr);

        CreateWindowW(L"BUTTON", L"Добавить сезон", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 10, 40, 120, 25,
                      hwnd, (HMENU)1, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Открыть тему", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 140, 40, 120, 25,
                      hwnd, (HMENU)6, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Справка", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 270, 40, 80, 25,
                      hwnd, (HMENU)7, nullptr, nullptr);

        g_hSeasonList = CreateWindowW(L"LISTBOX", nullptr, WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                                      10, 70, 180, 350, hwnd, (HMENU)5, nullptr, nullptr);

        g_hFileList = CreateWindowW(WC_LISTVIEWW, L"",
                                    WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT | LVS_OWNERDRAWFIXED | LVS_SINGLESEL | LVS_NOCOLUMNHEADER,
                                    220, 70, 540, 350, hwnd, nullptr, nullptr, nullptr);

        ListView_SetBkColor(g_hFileList, RGB(30,30,30));
        ListView_SetTextColor(g_hFileList, RGB(255,255,255));
        ListView_SetTextBkColor(g_hFileList, RGB(30,30,30));

        LVCOLUMNW col = {};
        col.mask = LVCF_WIDTH;
        col.cx = 520;
        SendMessageW(g_hFileList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);

        CreateWindowW(L"BUTTON", L"Добавить изображения", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 220, 430, 180, 25,
                      hwnd, (HMENU)3, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Создать .dww", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 600, 430, 120, 25,
                      hwnd, (HMENU)4, nullptr, nullptr);

        g_hStatus = CreateWindowW(L"STATIC", L"Готов", WS_VISIBLE | WS_CHILD, 10, 465, 700, 20,
                                  hwnd, nullptr, nullptr, nullptr);

        DragAcceptFiles(hwnd, TRUE);
        ApplyDarkTitleBar(hwnd);

        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);
        break;
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        DrawBackground((HDC)wParam, rc);
        return 1;
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

    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(30,30,30));
        SetTextColor(hdc, RGB(255,255,255));
        return (INT_PTR)CreateSolidBrush(RGB(30,30,30));
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlType == ODT_LISTVIEW) {
            int index = dis->itemID;
            int seasonIndex = SendMessageW(g_hSeasonList, LB_GETCURSEL, 0, 0);
            if (seasonIndex != LB_ERR && seasonIndex >= 0 && seasonIndex < (int)g_seasons.size()) {
                const auto& images = g_seasons[seasonIndex].images;
                if (index >= 0 && index < (int)images.size()) {
                    std::wstring imagePath = images[index];

                    HBITMAP hThumb = CreateThumbnail(imagePath, 200, 70);
                    if (hThumb) {
                        HDC hdc = dis->hDC;
                        RECT rc = dis->rcItem;
                        BITMAP bm;
                        GetObjectW(hThumb, sizeof(bm), &bm);
                        int x = rc.left + 5;
                        int y = rc.top + (rc.bottom - rc.top - bm.bmHeight) / 2;
                        HDC memDC = CreateCompatibleDC(hdc);
                        HGDIOBJ old = SelectObject(memDC, hThumb);
                        BitBlt(hdc, x, y, bm.bmWidth, bm.bmHeight, memDC, 0, 0, SRCCOPY);
                        SelectObject(memDC, old);
                        DeleteDC(memDC);
                        DeleteObject(hThumb);
                    }
                    SetBkMode(dis->hDC, TRANSPARENT);
                    SetTextColor(dis->hDC, RGB(255,255,255));
                    RECT textRect = dis->rcItem;
                    textRect.left += 210;
                    textRect.top += 5;
                    textRect.bottom = textRect.top + 20;
                    std::wstring fileName = fs::path(imagePath).filename().wstring();
                    DrawTextW(dis->hDC, fileName.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
                    return TRUE;
                }
            }
        } else {
            DrawOwnerButton(dis);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case 1: AddSeason(); break;
        case 3: AddImagesToSelectedSeason(); ShowSelectedSeasonImages(); break;
        case 4: {
            wchar_t buf[256];
            GetWindowTextW(g_hThemeNameEdit, buf, 256);
            g_themeName = buf;
            ExportTheme();
            break;
        }
        case 6: LoadThemeFromDww(); break;
        case 7: ShowHelpWindow(hwnd); break;
        case 5:
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                ShowSelectedSeasonImages();
            }
            if (HIWORD(wParam) == LBN_DBLCLK) {
                int sel = SendMessageW(g_hSeasonList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR && sel >= 0 && sel < (int)g_seasons.size()) {
                    OpenSeasonEditor(sel);
                }
            }
            break;
        }
        break;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < fileCount; ++i) {
            wchar_t path[MAX_PATH];
            if (DragQueryFileW(hDrop, i, path, MAX_PATH)) {
                DWORD attr = GetFileAttributesW(path);
                if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                    AddFolderToSelectedSeason(path);
                } else {
                    int sel = SendMessageW(g_hSeasonList, LB_GETCURSEL, 0, 0);
                    if (sel == LB_ERR || sel < 0 || sel >= (int)g_seasons.size()) {
                        Season s;
                        s.name = L"Сезон " + std::to_wstring(g_seasons.size() + 1);
                        g_seasons.push_back(s);
                        sel = (int)g_seasons.size() - 1;
                        UpdateSeasonList();
                        SendMessageW(g_hSeasonList, LB_SETCURSEL, sel, 0);
                    }
                    g_seasons[sel].images.push_back(path);
                }
            }
        }
        DragFinish(hDrop);
        ShowSelectedSeasonImages();
        SetWindowTextW(g_hStatus, L"Файлы добавлены.");
        break;
    }

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis->CtlType == ODT_LISTVIEW) {
            mis->itemHeight = 80;
            return TRUE;
        }
        break;
    }

    case WM_SIZE:
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ThemeBuilderGUI";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Конструктор тем", WS_OVERLAPPEDWINDOW,
                              100, 100, 800, 520, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        CoUninitialize();
        return 1;
    }

    // Устанавливаем иконку
    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(200));
    if (hIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}