#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include <Windows.h>

#include "blp_lib.h"

class BlpApi {
public:
    BlpApi();
    ~BlpApi();

    bool ensureLoaded(std::string* outError);
    bool isLoaded() const;
    std::string libraryPath() const;
    std::string version() const;

    BlpResult loadFromBuffer(const std::vector<uint8_t>& data, BlpImage* outImage);
    void freeImage(BlpImage* image);

    bool encodePngBytesToBlp(const std::vector<uint8_t>& pngBytes,
                             const std::string& outputPath,
                             int quality,
                             int mipCount,
                             std::string* outError);

    bool decodeMipToPngFromBuffer(const std::vector<uint8_t>& blpBytes,
                                  int mipIndex,
                                  const std::string& outputPath,
                                  std::string* outError);

private:
    using LoadFromBufferFn = BlpResult (*)(const uint8_t*, uint32_t, BlpImage*);
    using FreeImageFn = void (*)(BlpImage*);
    using GetVersionFn = const char* (*)();
    using EncodeBytesToBlpFn = BlpResult (*)(const uint8_t*, uint32_t, const char*, uint8_t, uint32_t);
    using DecodeMipToPngFromBufferFn = BlpResult (*)(const uint8_t*, uint32_t, uint32_t, const char*);

    bool resolveSymbols(std::string* outError);
    bool tryLoadLibrary(const std::wstring& path, std::string* outError);
    std::vector<std::wstring> candidateLibraryPaths() const;

    HMODULE module_ = nullptr;
    bool loaded_ = false;
    std::string loadedPath_;
    DWORD lastError_ = 0;

    LoadFromBufferFn loadFromBuffer_ = nullptr;
    FreeImageFn freeImage_ = nullptr;
    GetVersionFn getVersion_ = nullptr;
    EncodeBytesToBlpFn encodeBytesToBlp_ = nullptr;
    DecodeMipToPngFromBufferFn decodeMipToPngFromBuffer_ = nullptr;
};
