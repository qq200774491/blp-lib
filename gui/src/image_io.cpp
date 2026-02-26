#include "image_io.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "blp_api.h"
#include "utils.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

std::string extensionFromPath(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::string result = ext;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

void appendBytes(void* context, void* data, int size) {
    auto* out = reinterpret_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

std::vector<uint8_t> rgbaToRgbBytes(const RgbaImage& image) {
    std::vector<uint8_t> rgb;
    if (image.width <= 0 || image.height <= 0) return rgb;

    const int pixelCount = image.width * image.height;
    rgb.resize(pixelCount * 3);

    const uint8_t* src = image.pixels.data();
    uint8_t* dst = rgb.data();

    for (int i = 0; i < pixelCount; ++i) {
        dst[i * 3 + 0] = src[i * 4 + 0];
        dst[i * 3 + 1] = src[i * 4 + 1];
        dst[i * 3 + 2] = src[i * 4 + 2];
    }

    return rgb;
}

bool encodeToBytes(const RgbaImage& image,
                   const std::string& format,
                   int quality,
                   std::vector<uint8_t>* outBytes,
                   std::string* outError) {
    if (!outBytes) {
        if (outError) *outError = "输出缓冲区为空";
        return false;
    }

    outBytes->clear();

    if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
        if (outError) *outError = "无效的图像数据";
        return false;
    }

    const std::string fmt = normalizeFormat(format);

    if (fmt == "png") {
        const int stride = image.width * 4;
        int ok = stbi_write_png_to_func(appendBytes, outBytes,
                                        image.width, image.height, 4,
                                        image.pixels.data(), stride);
        if (!ok && outError) *outError = "PNG 编码失败";
        return ok != 0;
    }

    if (fmt == "tga") {
        int ok = stbi_write_tga_to_func(appendBytes, outBytes,
                                        image.width, image.height, 4,
                                        image.pixels.data());
        if (!ok && outError) *outError = "TGA 编码失败";
        return ok != 0;
    }

    if (fmt == "bmp") {
        const std::vector<uint8_t> rgb = rgbaToRgbBytes(image);
        int ok = stbi_write_bmp_to_func(appendBytes, outBytes,
                                        image.width, image.height, 3,
                                        rgb.data());
        if (!ok && outError) *outError = "BMP 编码失败";
        return ok != 0;
    }

    if (fmt == "jpg") {
        const std::vector<uint8_t> rgb = rgbaToRgbBytes(image);
        const int clampedQuality = std::clamp(quality, 1, 100);
        int ok = stbi_write_jpg_to_func(appendBytes, outBytes,
                                        image.width, image.height, 3,
                                        rgb.data(), clampedQuality);
        if (!ok && outError) *outError = "JPG 编码失败";
        return ok != 0;
    }

    if (outError) *outError = "不支持的输出格式";
    return false;
}

} // namespace

bool loadImageFile(const std::string& path,
                   RgbaImage* outImage,
                   ImageMeta* outMeta,
                   std::string* outError,
                   BlpApi* blpApi) {
    if (!outImage) {
        if (outError) *outError = "输出图像为空";
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        if (outError) *outError = "打开文件失败";
        return false;
    }

    const auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(bytes.data()), fileSize);
    file.close();

    if (bytes.empty()) {
        if (outError) *outError = "文件为空";
        return false;
    }

    const std::string ext = normalizeFormat(extensionFromPath(path));

    if (ext == "blp") {
        if (!blpApi || !blpApi->ensureLoaded(outError)) {
            if (outError && outError->empty()) *outError = "BLP 库未加载";
            return false;
        }

        BlpImage blpImage = {};
        const BlpResult result = blpApi->loadFromBuffer(bytes, &blpImage);
        if (result != BLP_SUCCESS) {
            if (outError) *outError = "BLP 解码失败";
            return false;
        }

        outImage->width = static_cast<int>(blpImage.width);
        outImage->height = static_cast<int>(blpImage.height);
        outImage->pixels.assign(blpImage.data, blpImage.data + blpImage.data_len);
        blpApi->freeImage(&blpImage);
    } else {
        int width = 0, height = 0, comp = 0;
        stbi_uc* data = stbi_load_from_memory(bytes.data(),
                                              static_cast<int>(bytes.size()),
                                              &width, &height, &comp, 4);
        if (!data) {
            if (outError) {
                const char* reason = stbi_failure_reason();
                *outError = std::string("图像解码失败") + (reason ? std::string("：") + reason : "");
            }
            return false;
        }

        outImage->width = width;
        outImage->height = height;
        outImage->pixels.assign(data, data + width * height * 4);
        stbi_image_free(data);
    }

    if (outMeta) {
        outMeta->width = outImage->width;
        outMeta->height = outImage->height;
        outMeta->format = ext;
        outMeta->fileSize = static_cast<uint64_t>(std::filesystem::file_size(path));
    }

    return true;
}

bool writeImageFile(const std::string& outputPath,
                    const std::string& format,
                    const RgbaImage& image,
                    int quality,
                    int mipCount,
                    std::string* outError,
                    BlpApi* blpApi) {
    const std::string fmt = normalizeFormat(format);
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(outputPath).parent_path());

    if (fmt == "blp") {
        if (!blpApi || !blpApi->ensureLoaded(outError)) {
            if (outError && outError->empty()) *outError = "BLP 库未加载";
            return false;
        }

        std::vector<uint8_t> pngBytes;
        if (!encodeToBytes(image, "png", 100, &pngBytes, outError)) {
            return false;
        }

        const int clampedQuality = std::clamp(quality, 0, 100);
        return blpApi->encodePngBytesToBlp(pngBytes, outputPath, clampedQuality, mipCount, outError);
    }

    std::vector<uint8_t> encoded;
    if (!encodeToBytes(image, fmt, quality, &encoded, outError)) {
        return false;
    }

    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        if (outError) *outError = "打开输出文件失败";
        return false;
    }

    file.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    if (!file.good()) {
        if (outError) *outError = "写入输出文件失败";
        return false;
    }

    return true;
}
