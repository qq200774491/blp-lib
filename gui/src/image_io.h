#pragma once

#include <string>
#include <vector>
#include <cstdint>

class BlpApi;

struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

struct ImageMeta {
    int width = 0;
    int height = 0;
    std::string format;
    uint64_t fileSize = 0;
};

bool loadImageFile(const std::string& path,
                   RgbaImage* outImage,
                   ImageMeta* outMeta,
                   std::string* outError,
                   BlpApi* blpApi);

bool writeImageFile(const std::string& outputPath,
                    const std::string& format,
                    const RgbaImage& image,
                    int quality,
                    int mipCount,
                    std::string* outError,
                    BlpApi* blpApi);
