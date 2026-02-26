#include "blp_api.h"

#include <cstdio>
#include <filesystem>

#include <Windows.h>

BlpApi::BlpApi() {
#ifdef BLP_STATIC_LINK
    loadFromBuffer_ = &blp_load_from_buffer;
    freeImage_ = &blp_free_image;
    getVersion_ = &blp_get_version;
    encodeBytesToBlp_ = &blp_encode_bytes_to_blp;
    decodeMipToPngFromBuffer_ = &blp_decode_mip_to_png_from_buffer;
    loaded_ = true;
    loadedPath_ = "static";
#endif
}

BlpApi::~BlpApi() {
#ifndef BLP_STATIC_LINK
    if (module_) {
        FreeLibrary(module_);
        module_ = nullptr;
    }
#endif
}

bool BlpApi::ensureLoaded(std::string* outError) {
#ifdef BLP_STATIC_LINK
    if (!loaded_) {
        loadFromBuffer_ = &blp_load_from_buffer;
        freeImage_ = &blp_free_image;
        getVersion_ = &blp_get_version;
        encodeBytesToBlp_ = &blp_encode_bytes_to_blp;
        decodeMipToPngFromBuffer_ = &blp_decode_mip_to_png_from_buffer;
        loaded_ = true;
        loadedPath_ = "static";
    }
    if (!loadFromBuffer_ || !freeImage_ || !getVersion_ || !encodeBytesToBlp_ ||
        !decodeMipToPngFromBuffer_) {
        if (outError) *outError = "静态链接的 BLP 符号缺失";
        return false;
    }
    return true;
#endif

    if (loaded_) return true;

    // Try BLP_LIB_PATH environment variable
    wchar_t envBuf[MAX_PATH] = {};
    DWORD envLen = GetEnvironmentVariableW(L"BLP_LIB_PATH", envBuf, MAX_PATH);
    if (envLen > 0 && envLen < MAX_PATH) {
        if (tryLoadLibrary(envBuf, outError)) return true;
    }

    // Try candidate paths
    const auto candidates = candidateLibraryPaths();
    for (const auto& path : candidates) {
        if (tryLoadLibrary(path, outError)) return true;
    }

    // Try system search
    const wchar_t* baseNames[] = {L"blp_lib", L"blp-windows"};
    for (const wchar_t* baseName : baseNames) {
        std::wstring dllName = std::wstring(baseName) + L".dll";
        HMODULE mod = LoadLibraryW(dllName.c_str());
        if (!mod) {
            lastError_ = GetLastError();
            continue;
        }
        module_ = mod;
        if (resolveSymbols(outError)) {
            loaded_ = true;
            // Convert wide path to UTF-8 for storage
            char narrowBuf[MAX_PATH * 3] = {};
            WideCharToMultiByte(CP_UTF8, 0, dllName.c_str(), -1, narrowBuf, sizeof(narrowBuf), nullptr, nullptr);
            loadedPath_ = narrowBuf;
            return true;
        }
        FreeLibrary(module_);
        module_ = nullptr;
    }

    if (outError) {
        std::string msg = "无法加载 BLP 库。已尝试路径：\n";
        for (const auto& c : candidates) {
            char narrow[MAX_PATH * 3] = {};
            WideCharToMultiByte(CP_UTF8, 0, c.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
            msg += "  " + std::string(narrow) + "\n";
        }
        char errBuf[64];
        std::snprintf(errBuf, sizeof(errBuf), "LastError: %lu", lastError_);
        msg += errBuf;
        *outError = msg;
    }
    return false;
}

bool BlpApi::isLoaded() const {
    return loaded_;
}

std::string BlpApi::libraryPath() const {
    return loadedPath_;
}

std::string BlpApi::version() const {
    if (getVersion_) return std::string(getVersion_());
    return {};
}

BlpResult BlpApi::loadFromBuffer(const std::vector<uint8_t>& data, BlpImage* outImage) {
    if (!loadFromBuffer_ || !outImage) return BLP_INVALID_INPUT;
    return loadFromBuffer_(data.data(), static_cast<uint32_t>(data.size()), outImage);
}

void BlpApi::freeImage(BlpImage* image) {
    if (freeImage_) freeImage_(image);
}

bool BlpApi::encodePngBytesToBlp(const std::vector<uint8_t>& pngBytes,
                                 const std::string& outputPath,
                                 int quality,
                                 int mipCount,
                                 std::string* outError) {
    if (!encodeBytesToBlp_) {
        if (outError) *outError = "BLP 编码入口缺失";
        return false;
    }

    const int clampedQuality = std::clamp(quality, 0, 100);
    const uint32_t clampedMipCount = static_cast<uint32_t>(std::max(1, mipCount));

    // Convert path separators
    std::string nativePath = outputPath;
    for (char& c : nativePath) {
        if (c == '/') c = '\\';
    }

    const BlpResult result = encodeBytesToBlp_(
        pngBytes.data(),
        static_cast<uint32_t>(pngBytes.size()),
        nativePath.c_str(),
        static_cast<uint8_t>(clampedQuality),
        clampedMipCount);

    if (result != BLP_SUCCESS) {
        if (outError) *outError = "BLP 编码失败";
        return false;
    }

    return true;
}

bool BlpApi::decodeMipToPngFromBuffer(const std::vector<uint8_t>& blpBytes,
                                      int mipIndex,
                                      const std::string& outputPath,
                                      std::string* outError) {
    if (!decodeMipToPngFromBuffer_) {
        if (outError) *outError = "BLP 解码入口缺失";
        return false;
    }

    const uint32_t clampedMip = static_cast<uint32_t>(std::max(0, mipIndex));
    std::string nativePath = outputPath;
    for (char& c : nativePath) {
        if (c == '/') c = '\\';
    }

    const BlpResult result = decodeMipToPngFromBuffer_(
        blpBytes.data(),
        static_cast<uint32_t>(blpBytes.size()),
        clampedMip,
        nativePath.c_str());

    if (result != BLP_SUCCESS) {
        if (outError) *outError = "BLP 层级解码失败";
        return false;
    }

    return true;
}

bool BlpApi::resolveSymbols(std::string* outError) {
    loadFromBuffer_ = reinterpret_cast<LoadFromBufferFn>(GetProcAddress(module_, "blp_load_from_buffer"));
    freeImage_ = reinterpret_cast<FreeImageFn>(GetProcAddress(module_, "blp_free_image"));
    getVersion_ = reinterpret_cast<GetVersionFn>(GetProcAddress(module_, "blp_get_version"));
    encodeBytesToBlp_ = reinterpret_cast<EncodeBytesToBlpFn>(GetProcAddress(module_, "blp_encode_bytes_to_blp"));
    decodeMipToPngFromBuffer_ = reinterpret_cast<DecodeMipToPngFromBufferFn>(
        GetProcAddress(module_, "blp_decode_mip_to_png_from_buffer"));

    if (!loadFromBuffer_ || !freeImage_ || !getVersion_ || !encodeBytesToBlp_ ||
        !decodeMipToPngFromBuffer_) {
        if (outError) *outError = "解析 BLP 符号失败";
        return false;
    }

    return true;
}

bool BlpApi::tryLoadLibrary(const std::wstring& path, std::string* outError) {
    if (path.empty()) return false;

    HMODULE mod = LoadLibraryW(path.c_str());
    if (!mod) {
        lastError_ = GetLastError();
        return false;
    }

    module_ = mod;
    if (!resolveSymbols(outError)) {
        FreeLibrary(module_);
        module_ = nullptr;
        return false;
    }

    loaded_ = true;
    char narrowBuf[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrowBuf, sizeof(narrowBuf), nullptr, nullptr);
    loadedPath_ = narrowBuf;
    return true;
}

std::vector<std::wstring> BlpApi::candidateLibraryPaths() const {
    const wchar_t* names[] = {L"blp_lib.dll", L"blp-windows.dll"};

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path appDir = std::filesystem::path(exePath).parent_path();

    wchar_t cwdBuf[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, cwdBuf);
    std::filesystem::path cwd(cwdBuf);

    std::vector<std::filesystem::path> dirs = {
        appDir,
        cwd,
        appDir / ".." / "target" / "release",
        appDir / ".." / "target" / "debug",
    };

    std::vector<std::wstring> candidates;
    for (const auto& dir : dirs) {
        for (const wchar_t* name : names) {
            candidates.push_back((dir / name).wstring());
        }
    }

    return candidates;
}
