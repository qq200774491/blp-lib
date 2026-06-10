#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "image_texture.h"

class BlpApi;

struct ThumbnailEntry {
    ImageTexture texture;  // valid iff loaded && !failed
    bool loaded = false;   // a load attempt finished (success or not)
    bool failed = false;
    int srcW = 0;
    int srcH = 0;
    uint64_t fileSize = 0;
    std::string extLabel;  // uppercase extension, shown as placeholder on failure
};

struct ThumbnailCache {
    std::unordered_map<std::string, ThumbnailEntry> entries;
    int generatedThumbPx = 0;  // pixel size entries were generated at (DPI guard)

    ThumbnailEntry* find(const std::string& path);
    // Synchronous decode + downscale + texture upload; callers budget per frame.
    ThumbnailEntry& loadNow(ID3D11Device* device, BlpApi* blpApi,
                            const std::string& path, int thumbPx);
    void invalidate(const std::string& path);
    void clear();
};
