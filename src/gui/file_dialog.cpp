/**
 * @file file_dialog.cpp
 * @brief Modern Windows File Explorer dialogs using the IFileOpenDialog COM API.
 */

#include "file_dialog.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h> // Modern IFileOpenDialog, IShellItem
#include <string>
#include <vector>

namespace grn {

static std::wstring to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (count <= 0) return L"";
    std::wstring wstr(count - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], count);
    return wstr;
}

static std::string to_utf8(const wchar_t* wstr) {
    if (!wstr) return "";
    int count = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return "";
    std::string str(count - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], count, nullptr, nullptr);
    return str;
}

std::optional<std::string> open_file_dialog(const char* filter, const char* title) {
    HRESULT hr_com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool need_uninit = SUCCEEDED(hr_com) && (hr_com != S_FALSE);

    std::optional<std::string> result = std::nullopt;
    IFileOpenDialog* pFileOpen = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                  IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

    if (SUCCEEDED(hr) && pFileOpen) {
        DWORD dwOptions = 0;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
            pFileOpen->SetOptions(dwOptions | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
        }

        if (title && *title) {
            std::wstring wtitle = to_wstring(title);
            pFileOpen->SetTitle(wtitle.c_str());
        }

        // Set up filters
        bool is_anim = filter && std::string(filter).find("*.grn") != std::string::npos &&
                       std::string(filter).find("Animation") != std::string::npos;

        if (is_anim) {
            COMDLG_FILTERSPEC animTypes[] = {
                { L"GRN Animation Tracks (*.grn)", L"*.grn" },
                { L"All Files (*.*)", L"*.*" }
            };
            pFileOpen->SetFileTypes(ARRAYSIZE(animTypes), animTypes);
        } else {
            COMDLG_FILTERSPEC modelTypes[] = {
                { L"All Supported 3D Models (*.grn; *.glb; *.gltf)", L"*.grn;*.glb;*.gltf" },
                { L"GRN Models (*.grn)", L"*.grn" },
                { L"glTF 2.0 Binary / JSON (*.glb; *.gltf)", L"*.glb;*.gltf" },
                { L"All Files (*.*)", L"*.*" }
            };
            pFileOpen->SetFileTypes(ARRAYSIZE(modelTypes), modelTypes);
        }

        if (SUCCEEDED(pFileOpen->Show(nullptr))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem)) && pItem) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)) && pszFilePath) {
                    result = to_utf8(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }

    if (need_uninit) {
        CoUninitialize();
    }

    return result;
}

std::optional<std::string> open_folder_dialog(const char* title) {
    HRESULT hr_com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool need_uninit = SUCCEEDED(hr_com) && (hr_com != S_FALSE);

    std::optional<std::string> result = std::nullopt;
    IFileOpenDialog* pFolderDialog = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                  IID_IFileOpenDialog, reinterpret_cast<void**>(&pFolderDialog));

    if (SUCCEEDED(hr) && pFolderDialog) {
        DWORD dwOptions = 0;
        if (SUCCEEDED(pFolderDialog->GetOptions(&dwOptions))) {
            // FOS_PICKFOLDERS opens the full modern Windows Explorer folder picker
            pFolderDialog->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        }

        if (title && *title) {
            std::wstring wtitle = to_wstring(title);
            pFolderDialog->SetTitle(wtitle.c_str());
        }

        if (SUCCEEDED(pFolderDialog->Show(nullptr))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFolderDialog->GetResult(&pItem)) && pItem) {
                PWSTR pszFolderPath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath)) && pszFolderPath) {
                    result = to_utf8(pszFolderPath);
                    CoTaskMemFree(pszFolderPath);
                }
                pItem->Release();
            }
        }
        pFolderDialog->Release();
    }

    if (need_uninit) {
        CoUninitialize();
    }

    return result;
}

} // namespace grn

#else

namespace grn {
std::optional<std::string> open_file_dialog(const char*, const char*) { return std::nullopt; }
std::optional<std::string> open_folder_dialog(const char*) { return std::nullopt; }
}

#endif
