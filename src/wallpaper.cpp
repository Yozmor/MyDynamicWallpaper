#include "wallpaper.h"
#include <windows.h>
#include <shobjidl.h>
#include <string>

bool WallpaperChanger::setWallpaper(const std::wstring& imagePath) {
    IDesktopWallpaper* pWallpaper = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pWallpaper));
    if (FAILED(hr)) return false;

    hr = pWallpaper->SetWallpaper(nullptr, imagePath.c_str());
    pWallpaper->Release();
    return SUCCEEDED(hr);
}