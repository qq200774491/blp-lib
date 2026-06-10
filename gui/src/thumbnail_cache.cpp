#include "thumbnail_cache.h"

#include <algorithm>
#include <cctype>

#include "image_io.h"
#include "image_ops.h"
#include "utils.h"

ThumbnailEntry* ThumbnailCache::find(const std::string& path) {
    auto it = entries.find(path);
    return it != entries.end() ? &it->second : nullptr;
}

ThumbnailEntry& ThumbnailCache::loadNow(ID3D11Device* device, BlpApi* blpApi,
                                        const std::string& path, int thumbPx) {
    ThumbnailEntry& entry = entries[path];
    if (entry.loaded) return entry;
    entry.loaded = true;

    std::string ext = fsPathToUtf8(fsPathFromUtf8(path).extension());
    if (!ext.empty() && ext[0] == '.') ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    entry.extLabel = ext;

    RgbaImage img;
    ImageMeta meta;
    std::string error;
    if (!loadImageFile(path, &img, &meta, &error, blpApi)) {
        entry.failed = true;
        return entry;
    }
    entry.srcW = meta.width;
    entry.srcH = meta.height;
    entry.fileSize = meta.fileSize;

    RgbaImage thumb = transformToTarget(img.pixels.data(), img.width, img.height,
                                        thumbPx, thumbPx, ResizeMethod::CenterTransparent);
    if (thumb.width <= 0 || !device ||
        !entry.texture.createFromRGBA(device, thumb.pixels.data(), thumb.width, thumb.height)) {
        entry.failed = true;
    }
    return entry;
}

void ThumbnailCache::invalidate(const std::string& path) {
    auto it = entries.find(path);
    if (it == entries.end()) return;
    it->second.texture.release();
    entries.erase(it);
}

void ThumbnailCache::clear() {
    for (auto& kv : entries) {
        kv.second.texture.release();
    }
    entries.clear();
}
