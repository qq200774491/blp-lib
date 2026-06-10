// blp_codec 自测：
//   1. 解码回归：遍历 <blp_dir> 下所有 .blp，解码后与 <png_dir> 中同名参考 PNG 逐像素比对（PSNR）。
//   2. 编码往返：把解码结果编码为 JPEG-BLP1（quality 90，完整 mip 链），再解码与原图比 PSNR，
//      并逐级解码 mip 验证链完整。
// 用法：blp_codec_selftest <blp_dir> <png_dir>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "blp/blp_codec.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

// 仅比较 RGB（参考 PNG 可能不带与 BLP 完全一致的 alpha 语义），alpha 单独报告
double psnrRgb(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t pixelCount) {
    double mse = 0.0;
    for (size_t i = 0; i < pixelCount; ++i) {
        for (int c = 0; c < 3; ++c) {
            const double d = static_cast<double>(a[i * 4 + c]) - b[i * 4 + c];
            mse += d * d;
        }
    }
    mse /= static_cast<double>(pixelCount * 3);
    if (mse <= 0.0) return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: blp_codec_selftest <blp_dir> <png_dir>\n");
        return 2;
    }

    const fs::path blpDir = argv[1];
    const fs::path pngDir = argv[2];

    // 收集参考 PNG（按文件名 stem 索引）
    std::map<std::string, fs::path> referencePngs;
    for (const auto& entry : fs::recursive_directory_iterator(pngDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".png") {
            referencePngs[entry.path().stem().string()] = entry.path();
        }
    }

    int failures = 0;
    int tested = 0;

    for (const auto& entry : fs::recursive_directory_iterator(blpDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".blp") continue;
        const std::string name = entry.path().stem().string();
        ++tested;

        const std::vector<uint8_t> blpBytes = readFile(entry.path());
        std::string error;
        auto decoded = blpcodec::decode(blpBytes.data(), blpBytes.size(), &error);
        if (!decoded) {
            std::printf("[FAIL] %s: decode error: %s\n", name.c_str(), error.c_str());
            ++failures;
            continue;
        }

        // --- 与参考 PNG 比对 ---
        auto refIt = referencePngs.find(name);
        if (refIt == referencePngs.end()) {
            std::printf("[WARN] %s: no reference png, decoded %ux%u\n",
                        name.c_str(), decoded->width, decoded->height);
        } else {
            int rw = 0, rh = 0, comp = 0;
            const std::vector<uint8_t> pngBytes = readFile(refIt->second);
            stbi_uc* ref = stbi_load_from_memory(pngBytes.data(), static_cast<int>(pngBytes.size()),
                                                 &rw, &rh, &comp, 4);
            if (!ref) {
                std::printf("[WARN] %s: reference png unreadable\n", name.c_str());
            } else if (static_cast<uint32_t>(rw) != decoded->width ||
                       static_cast<uint32_t>(rh) != decoded->height) {
                std::printf("[FAIL] %s: size mismatch blp=%ux%u png=%dx%d\n",
                            name.c_str(), decoded->width, decoded->height, rw, rh);
                ++failures;
            } else {
                std::vector<uint8_t> refVec(ref, ref + static_cast<size_t>(rw) * rh * 4);
                const double psnr = psnrRgb(decoded->rgba, refVec,
                                            static_cast<size_t>(rw) * rh);
                const bool pass = psnr > 40.0;
                std::printf("[%s] %s: decode vs reference PSNR=%.1f dB (%ux%u)\n",
                            pass ? " OK " : "FAIL", name.c_str(), psnr, rw, rh);
                if (!pass) ++failures;
            }
            if (ref) stbi_image_free(ref);
        }

        // --- 编码往返 ---
        std::vector<uint8_t> encoded;
        if (!blpcodec::encodeJpegBlp1(decoded->rgba.data(), decoded->width, decoded->height,
                                      90, 16, encoded, &error)) {
            std::printf("[FAIL] %s: encode error: %s\n", name.c_str(), error.c_str());
            ++failures;
            continue;
        }

        auto roundtrip = blpcodec::decode(encoded.data(), encoded.size(), &error);
        if (!roundtrip) {
            std::printf("[FAIL] %s: roundtrip decode error: %s\n", name.c_str(), error.c_str());
            ++failures;
            continue;
        }
        if (roundtrip->width != decoded->width || roundtrip->height != decoded->height) {
            std::printf("[FAIL] %s: roundtrip size mismatch\n", name.c_str());
            ++failures;
            continue;
        }

        const double rtPsnr = psnrRgb(decoded->rgba, roundtrip->rgba,
                                      static_cast<size_t>(decoded->width) * decoded->height);
        // 全部 mip 逐级解码
        int mipOk = 0, mipFail = 0;
        for (uint32_t level = 0; level < 16; ++level) {
            std::string mipError;
            auto mip = blpcodec::decodeMip(encoded.data(), encoded.size(), level, &mipError);
            if (mip) {
                ++mipOk;
            } else if (mipError != "无效的 mipmap 级别") {
                ++mipFail;
            }
        }
        const bool rtPass = rtPsnr > 30.0 && mipFail == 0;
        std::printf("[%s] %s: encode roundtrip PSNR=%.1f dB, mips ok=%d fail=%d, size=%zu bytes\n",
                    rtPass ? " OK " : "FAIL", name.c_str(), rtPsnr, mipOk, mipFail, encoded.size());
        if (!rtPass) ++failures;
    }

    std::printf("\n%d blp tested, %d failure(s)\n", tested, failures);
    return failures == 0 ? 0 : 1;
}
