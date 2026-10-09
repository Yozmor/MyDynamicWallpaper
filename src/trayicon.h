#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>

class TrayIcon {
public:
    TrayIcon(HWND hwnd, UINT message);
    ~TrayIcon();

    // Показать иконку в трее
    bool show();

    // Обновить иконку (например, если нужно изменить)
    bool update();

    // Скрыть иконку
    bool hide();

    // Показать контекстное меню
    void showMenu(bool autoStart);

private:
    HWND m_hwnd;
    UINT m_message;
    NOTIFYICONDATA m_nid;
};