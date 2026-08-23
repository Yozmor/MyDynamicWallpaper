#include "trayicon.h"
#include "resource.h"

TrayIcon::TrayIcon(HWND hwnd, UINT message)
    : m_hwnd(hwnd), m_message(message) {
    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATA);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = m_message;
    m_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));
    wcscpy_s(m_nid.szTip, L"My Dynamic Wallpaper");
}

TrayIcon::~TrayIcon() {
    hide();
}

bool TrayIcon::show() {
    return Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
}

bool TrayIcon::update() {
    return Shell_NotifyIconW(NIM_MODIFY, &m_nid) != FALSE;
}

bool TrayIcon::hide() {
    return Shell_NotifyIconW(NIM_DELETE, &m_nid) != FALSE;
}

void TrayIcon::showMenu(bool autoStart) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, 1, L"Выбрать тему...");
    AppendMenuW(hMenu, MF_STRING, 4, L"Открыть интерфейс");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Пункт "Автозапуск" с галочкой
    UINT flags = MF_STRING;
    if (autoStart) flags |= MF_CHECKED;
    AppendMenuW(hMenu, flags, 3, L"Автозапуск");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 2, L"Выход");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
}