#include "file_dialogs.h"

#include <ShObjIdl.h>
#include <commdlg.h>

namespace {

void initCom() {
    static bool initialized = false;
    if (!initialized) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        initialized = true;
    }
}

} // namespace

std::vector<std::wstring> openFileDialog(HWND hwnd, const wchar_t* filter, bool multiSelect) {
    initCom();
    std::vector<std::wstring> results;

    IFileOpenDialog* pDialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&pDialog));
    if (FAILED(hr)) return results;

    DWORD options = 0;
    pDialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM;
    if (multiSelect) options |= FOS_ALLOWMULTISELECT;
    pDialog->SetOptions(options);

    // Parse filter string to COMDLG_FILTERSPEC
    // Input format: "Description\0*.ext1;*.ext2\0\0"
    // We need to convert to COMDLG_FILTERSPEC array
    COMDLG_FILTERSPEC filterSpec = {};
    filterSpec.pszName = L"图片文件";
    filterSpec.pszSpec = filter ? filter : L"*.*";
    pDialog->SetFileTypes(1, &filterSpec);

    hr = pDialog->Show(hwnd);
    if (SUCCEEDED(hr)) {
        if (multiSelect) {
            IShellItemArray* pItems = nullptr;
            hr = pDialog->GetResults(&pItems);
            if (SUCCEEDED(hr)) {
                DWORD count = 0;
                pItems->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pItems->GetItemAt(i, &pItem))) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            results.push_back(path);
                            CoTaskMemFree(path);
                        }
                        pItem->Release();
                    }
                }
                pItems->Release();
            }
        } else {
            IShellItem* pItem = nullptr;
            hr = pDialog->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                PWSTR path = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    results.push_back(path);
                    CoTaskMemFree(path);
                }
                pItem->Release();
            }
        }
    }

    pDialog->Release();
    return results;
}

std::wstring openFolderDialog(HWND hwnd) {
    initCom();

    IFileOpenDialog* pDialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&pDialog));
    if (FAILED(hr)) return {};

    DWORD options = 0;
    pDialog->GetOptions(&options);
    pDialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

    hr = pDialog->Show(hwnd);
    std::wstring result;
    if (SUCCEEDED(hr)) {
        IShellItem* pItem = nullptr;
        hr = pDialog->GetResult(&pItem);
        if (SUCCEEDED(hr)) {
            PWSTR path = nullptr;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            pItem->Release();
        }
    }

    pDialog->Release();
    return result;
}

std::wstring saveFileDialog(HWND hwnd, const wchar_t* filter, const wchar_t* defaultExt) {
    initCom();

    IFileSaveDialog* pDialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&pDialog));
    if (FAILED(hr)) return {};

    DWORD options = 0;
    pDialog->GetOptions(&options);
    pDialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);

    if (defaultExt) pDialog->SetDefaultExtension(defaultExt);

    COMDLG_FILTERSPEC filterSpec = {};
    filterSpec.pszName = L"文件";
    filterSpec.pszSpec = filter ? filter : L"*.*";
    pDialog->SetFileTypes(1, &filterSpec);

    hr = pDialog->Show(hwnd);
    std::wstring result;
    if (SUCCEEDED(hr)) {
        IShellItem* pItem = nullptr;
        hr = pDialog->GetResult(&pItem);
        if (SUCCEEDED(hr)) {
            PWSTR path = nullptr;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            pItem->Release();
        }
    }

    pDialog->Release();
    return result;
}
