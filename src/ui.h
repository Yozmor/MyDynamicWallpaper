#pragma once
#include <windows.h>
#include <string>

// Открывает главное окно интерфейса
bool OpenMainWindow(HINSTANCE hInstance, HWND parent);

// Показывает модальный диалог координат
bool ShowLocationDialog(HINSTANCE hInstance, HWND parent,
                        double& latitude, double& longitude);