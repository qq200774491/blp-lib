#include "win_integration.h"

#include <Shlwapi.h>
#include <ShlObj.h>
#include <filesystem>

#pragma comment(lib, "Shlwapi.lib")

namespace {

const wchar_t* kBlpProgId = L"BLPViewer.File";
const wchar_t* kThumbnailClsid = L"{27A35239-0B87-4085-8944-463B440D162F}";
const wchar_t* kThumbnailHandler = L"{E357FCCD-A995-4576-B01F-234630154E96}";

bool regSetString(HKEY hKey, const wchar_t* valueName, const wchar_t* data) {
    return RegSetValueExW(hKey, valueName, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(data),
                          static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

} // namespace

std::wstring getAppPath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

std::wstring getAppDir() {
    return std::filesystem::path(getAppPath()).parent_path().wstring();
}

bool isBlpAssociated() {
    wchar_t buffer[256] = {};
    DWORD size = ARRAYSIZE(buffer);
    HRESULT hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID,
                                   L".blp", nullptr, buffer, &size);
    if (SUCCEEDED(hr)) {
        return _wcsicmp(buffer, kBlpProgId) == 0;
    }

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Classes\\.blp", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t val[256] = {};
        DWORD valSize = sizeof(val);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, nullptr, nullptr, &type,
                             reinterpret_cast<BYTE*>(val), &valSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return _wcsicmp(val, kBlpProgId) == 0;
        }
        RegCloseKey(hKey);
    }

    return false;
}

bool registerBlpAssociation(const std::wstring& appPath, std::string* outError) {
    auto createKeyAndSet = [](const wchar_t* subKey, const wchar_t* value) -> bool {
        HKEY hKey = nullptr;
        DWORD disposition = 0;
        std::wstring fullKey = std::wstring(L"Software\\Classes\\") + subKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, fullKey.c_str(), 0, nullptr,
                           REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                           &hKey, &disposition) != ERROR_SUCCESS) {
            return false;
        }
        bool ok = regSetString(hKey, nullptr, value);
        RegCloseKey(hKey);
        return ok;
    };

    // .blp -> BLPViewer.File
    {
        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.blp", 0, nullptr,
                           REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                           &hKey, nullptr) != ERROR_SUCCESS) {
            if (outError) *outError = "写入注册表失败";
            return false;
        }
        regSetString(hKey, nullptr, kBlpProgId);
        regSetString(hKey, L"PerceivedType", L"image");
        regSetString(hKey, L"Content Type", L"image/blp");
        RegCloseKey(hKey);
    }

    // OpenWithProgids
    createKeyAndSet(L".blp\\OpenWithProgids", L"");
    {
        HKEY hKey = nullptr;
        std::wstring key = L"Software\\Classes\\.blp\\OpenWithProgids";
        if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, kBlpProgId, 0, REG_SZ, reinterpret_cast<const BYTE*>(L""), sizeof(wchar_t));
            RegCloseKey(hKey);
        }
    }

    // ProgId
    createKeyAndSet(L"BLPViewer.File", L"BLP Image");

    // DefaultIcon
    std::wstring iconValue = appPath + L",0";
    createKeyAndSet(L"BLPViewer.File\\DefaultIcon", iconValue.c_str());

    // shell/open/command
    std::wstring cmdValue = L"\"" + appPath + L"\" \"%1\"";
    createKeyAndSet(L"BLPViewer.File\\shell\\open\\command", cmdValue.c_str());

    // Applications entry
    std::wstring exeName = std::filesystem::path(appPath).filename().wstring();
    std::wstring appKey = L"Applications\\" + exeName + L"\\shell\\open\\command";
    createKeyAndSet(appKey.c_str(), cmdValue.c_str());

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool isThumbnailRegistered() {
    std::wstring key = std::wstring(L"Software\\Classes\\.blp\\ShellEx\\") + kThumbnailHandler;
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t val[256] = {};
    DWORD valSize = sizeof(val);
    DWORD type = 0;
    bool result = false;
    if (RegQueryValueExW(hKey, nullptr, nullptr, &type,
                         reinterpret_cast<BYTE*>(val), &valSize) == ERROR_SUCCESS) {
        result = (_wcsicmp(val, kThumbnailClsid) == 0);
    }
    RegCloseKey(hKey);
    return result;
}

bool callDllEntry(const std::wstring& dllPath, const char* entry, std::string* outError) {
    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (!module) {
        if (outError) *outError = "加载缩略图 DLL 失败";
        return false;
    }

    auto* func = reinterpret_cast<HRESULT(STDAPICALLTYPE*)(void)>(
        GetProcAddress(module, entry));
    if (!func) {
        if (outError) *outError = "找不到注册入口";
        FreeLibrary(module);
        return false;
    }

    HRESULT hr = func();
    FreeLibrary(module);
    if (FAILED(hr)) {
        if (outError) *outError = "注册调用失败";
        return false;
    }
    return true;
}
