// BLP1 编解码实现。
// 解码移植自 ShadowEngine（src/parser/BlpDecoder.cpp），修正了边界检查；
// 编码为新实现：JPEG-content BLP1，每个 mip 写自包含完整 JPEG（jpegHeaderSize=0）。
#include "blp_codec.h"

#include <algorithm>
#include <cstring>

#include <turbojpeg.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace blpcodec {

namespace {

constexpr uint32_t MAGIC_BLP1 = 0x31504C42; // 'BLP1'
constexpr uint32_t MAGIC_BLP2 = 0x32504C42; // 'BLP2'
constexpr size_t HEADER_SIZE = 156;         // 固定头 + 16 个 mip offset/size
constexpr uint32_t COMPRESSION_JPEG = 0;
constexpr uint32_t COMPRESSION_PALETTED = 1;
constexpr int MAX_MIPS = 16;

struct BlpHeader {
    uint32_t magic = 0;
    uint32_t compression = 0;
    uint32_t alphaBits = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t extra = 0;
    uint32_t hasMipmaps = 0;
    uint32_t mipmapOffsets[MAX_MIPS] = {};
    uint32_t mipmapSizes[MAX_MIPS] = {};
};

void setError(std::string* outError, const std::string& message) {
    if (outError) *outError = message;
}

uint32_t readU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

bool parseHeader(const uint8_t* data, size_t size, BlpHeader& header, std::string* outError) {
    if (!data || size < HEADER_SIZE) {
        setError(outError, "文件太小，不是有效的 BLP 文件");
        return false;
    }

    header.magic = readU32(data);
    if (header.magic == MAGIC_BLP2) {
        setError(outError, "不支持 BLP2 格式（仅支持 War3 的 BLP1）");
        return false;
    }
    if (header.magic != MAGIC_BLP1) {
        setError(outError, "无效的 BLP 文件头");
        return false;
    }

    header.compression = readU32(data + 4);
    header.alphaBits = readU32(data + 8);
    header.width = readU32(data + 12);
    header.height = readU32(data + 16);
    header.extra = readU32(data + 20);
    header.hasMipmaps = readU32(data + 24);

    for (int i = 0; i < MAX_MIPS; ++i) {
        header.mipmapOffsets[i] = readU32(data + 28 + i * 4);
        header.mipmapSizes[i] = readU32(data + 28 + 64 + i * 4);
    }

    return true;
}

// 校验 mip 数据段在文件范围内，返回指向段起始的指针
const uint8_t* mipSegment(const uint8_t* data, size_t size, const BlpHeader& header,
                          uint32_t mipLevel, uint32_t* outMipSize, std::string* outError) {
    const uint32_t offset = header.mipmapOffsets[mipLevel];
    const uint32_t mipSize = header.mipmapSizes[mipLevel];

    if (offset == 0 || mipSize == 0) {
        setError(outError, "无效的 mipmap 级别");
        return nullptr;
    }
    if (static_cast<uint64_t>(offset) + mipSize > size) {
        setError(outError, "Mipmap 数据超出文件范围");
        return nullptr;
    }

    *outMipSize = mipSize;
    return data + offset;
}

std::optional<RawImage> decodePaletted(const uint8_t* data, size_t size, const BlpHeader& header, uint32_t mipLevel, std::string* outError) {
    constexpr size_t PALETTE_OFFSET = HEADER_SIZE;
    constexpr size_t PALETTE_SIZE = 256 * 4;

    if (size < PALETTE_OFFSET + PALETTE_SIZE) {
        setError(outError, "文件太小，无法读取调色板");
        return std::nullopt;
    }
    const uint8_t* palette = data + PALETTE_OFFSET; // 256 色，BGRA

    const uint32_t width = std::max(1u, header.width >> mipLevel);
    const uint32_t height = std::max(1u, header.height >> mipLevel);
    const size_t pixelCount = static_cast<size_t>(width) * height;

    uint32_t mipSize = 0;
    const uint8_t* indexData = mipSegment(data, size, header, mipLevel, &mipSize, outError);
    if (!indexData) return std::nullopt;

    if (mipSize < pixelCount) {
        setError(outError, "Mipmap 数据不足以容纳索引数据");
        return std::nullopt;
    }

    RawImage image;
    image.width = width;
    image.height = height;
    image.rgba.resize(pixelCount * 4);

    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t* color = palette + indexData[i] * 4;
        image.rgba[i * 4 + 0] = color[2]; // R
        image.rgba[i * 4 + 1] = color[1]; // G
        image.rgba[i * 4 + 2] = color[0]; // B
        image.rgba[i * 4 + 3] = 255;
    }

    if (header.alphaBits > 0) {
        // Alpha 数据紧跟在索引数据之后（同一 mip 段内）
        const uint8_t* alphaData = indexData + pixelCount;
        const size_t remaining = mipSize - pixelCount;

        if (header.alphaBits == 8 && remaining >= pixelCount) {
            for (size_t i = 0; i < pixelCount; ++i) {
                image.rgba[i * 4 + 3] = alphaData[i];
            }
        } else if (header.alphaBits == 4 && remaining >= (pixelCount + 1) / 2) {
            for (size_t i = 0; i < pixelCount; ++i) {
                const uint8_t alphaByte = alphaData[i / 2];
                const uint8_t alpha4 = (i % 2 == 0) ? (alphaByte & 0x0F) : (alphaByte >> 4);
                image.rgba[i * 4 + 3] = static_cast<uint8_t>(alpha4 * 17);
            }
        } else if (header.alphaBits == 1 && remaining >= (pixelCount + 7) / 8) {
            for (size_t i = 0; i < pixelCount; ++i) {
                const uint8_t alphaByte = alphaData[i / 8];
                image.rgba[i * 4 + 3] = ((alphaByte >> (i % 8)) & 1) ? 255 : 0;
            }
        }
    }

    return image;
}

std::optional<RawImage> decodeJpeg(const uint8_t* data, size_t size,  const BlpHeader& header, uint32_t mipLevel,  std::string* outError) {
    // JPEG-content BLP1：头部之后是 uint32 共享 JPEG 头大小 + 共享头字节，
    // 每个 mip 段是共享头 + 该 mip 的扫描数据拼出来的完整 JPEG。
    if (size < HEADER_SIZE + 4) {
        setError(outError, "文件太小，无法读取 JPEG 头部大小");
        return std::nullopt;
    }

    const uint32_t jpegHeaderSize = readU32(data + HEADER_SIZE);
    if (static_cast<uint64_t>(HEADER_SIZE) + 4 + jpegHeaderSize > size) {
        setError(outError, "文件太小，无法读取 JPEG 头部");
        return std::nullopt;
    }
    const uint8_t* jpegHeader = data + HEADER_SIZE + 4;

    uint32_t mipSize = 0;
    const uint8_t* mipData = mipSegment(data, size, header, mipLevel, &mipSize, outError);
    if (!mipData) return std::nullopt;

    std::vector<uint8_t> jpegData;
    jpegData.reserve(jpegHeaderSize + mipSize);
    jpegData.insert(jpegData.end(), jpegHeader, jpegHeader + jpegHeaderSize);
    jpegData.insert(jpegData.end(), mipData, mipData + mipSize);

    tjhandle tjHandle = tjInitDecompress();
    if (!tjHandle) {
        setError(outError, "turbojpeg 初始化失败");
        return std::nullopt;
    }

    int imgWidth = 0, imgHeight = 0, jpegSubsamp = 0, jpegColorspace = 0;
    if (tjDecompressHeader3(tjHandle, jpegData.data(), static_cast<unsigned long>(jpegData.size()),
                            &imgWidth, &imgHeight, &jpegSubsamp, &jpegColorspace) != 0) {
        setError(outError, std::string("turbojpeg 头部解析失败: ") + tjGetErrorStr());
        tjDestroy(tjHandle);
        return std::nullopt;
    }

    RawImage image;
    image.width = static_cast<uint32_t>(imgWidth);
    image.height = static_cast<uint32_t>(imgHeight);
    const size_t pixelCount = static_cast<size_t>(imgWidth) * imgHeight;
    image.rgba.resize(pixelCount * 4);

    // BLP1/JPEG 的像素语义按 War3/旧 blp crate 对齐：
    //   4 分量（标准 War3 贴图）：用 CMYK/YCCK 直通拿到 4 个原始分量，再按 BGRA 解释
    //   3 分量（第三方工具，分量 ID 伪装成 'R','G','B' 触发直通）：拿到 BGR，无 alpha
    //   1 分量：灰度
    if (jpegColorspace == TJCS_CMYK || jpegColorspace == TJCS_YCCK) {
        if (tjDecompress2(tjHandle, jpegData.data(), static_cast<unsigned long>(jpegData.size()),
                          image.rgba.data(), imgWidth, 0, imgHeight, TJPF_CMYK, 0) != 0) {
            setError(outError, std::string("turbojpeg 解码失败: ") + tjGetErrorStr());
            tjDestroy(tjHandle);
            return std::nullopt;
        }
        tjDestroy(tjHandle);
        // BGRA → RGBA
        for (size_t i = 0; i < pixelCount; ++i) {
            std::swap(image.rgba[i * 4 + 0], image.rgba[i * 4 + 2]);
        }
    } else if (jpegColorspace == TJCS_GRAY) {
        std::vector<uint8_t> gray(pixelCount);
        if (tjDecompress2(tjHandle, jpegData.data(), static_cast<unsigned long>(jpegData.size()),
                          gray.data(), imgWidth, 0, imgHeight, TJPF_GRAY, 0) != 0) {
            setError(outError, std::string("turbojpeg 解码失败: ") + tjGetErrorStr());
            tjDestroy(tjHandle);
            return std::nullopt;
        }
        tjDestroy(tjHandle);
        for (size_t i = 0; i < pixelCount; ++i) {
            image.rgba[i * 4 + 0] = gray[i];
            image.rgba[i * 4 + 1] = gray[i];
            image.rgba[i * 4 + 2] = gray[i];
            image.rgba[i * 4 + 3] = 255;
        }
    } else {
        // TJCS_RGB（直通）或 TJCS_YCbCr（转换后）：War3 都按 BGR 解释
        std::vector<uint8_t> bgr(pixelCount * 3);
        if (tjDecompress2(tjHandle, jpegData.data(), static_cast<unsigned long>(jpegData.size()),
                          bgr.data(), imgWidth, 0, imgHeight, TJPF_RGB, 0) != 0) {
            setError(outError, std::string("turbojpeg 解码失败: ") + tjGetErrorStr());
            tjDestroy(tjHandle);
            return std::nullopt;
        }
        tjDestroy(tjHandle);
        for (size_t i = 0; i < pixelCount; ++i) {
            image.rgba[i * 4 + 0] = bgr[i * 3 + 2]; // R
            image.rgba[i * 4 + 1] = bgr[i * 3 + 1]; // G
            image.rgba[i * 4 + 2] = bgr[i * 3 + 0]; // B
            image.rgba[i * 4 + 3] = 255;
        }
    }

    return image;
}

void writeU32(std::vector<uint8_t>& out, size_t pos, uint32_t value) {
    std::memcpy(out.data() + pos, &value, sizeof(value));
}

} // namespace

bool isBlp1(const uint8_t* data, size_t size) {
    return data && size >= 4 && readU32(data) == MAGIC_BLP1;
}

bool isBlpMagic(const uint8_t* data, size_t size) {
    if (!data || size < 4) return false;
    const uint32_t magic = readU32(data);
    return magic == MAGIC_BLP1 || magic == MAGIC_BLP2;
}

std::optional<RawImage> decodeMip(const uint8_t* data, size_t size,
                                  uint32_t mipLevel, std::string* outError) {
    if (mipLevel >= MAX_MIPS) {
        setError(outError, "mipmap 级别超出范围");
        return std::nullopt;
    }

    BlpHeader header;
    if (!parseHeader(data, size, header, outError)) {
        return std::nullopt;
    }

    if (header.compression == COMPRESSION_PALETTED) {
        return decodePaletted(data, size, header, mipLevel, outError);
    }
    if (header.compression == COMPRESSION_JPEG) {
        return decodeJpeg(data, size, header, mipLevel, outError);
    }

    setError(outError, "不支持的 BLP 压缩类型: " + std::to_string(header.compression));
    return std::nullopt;
}

std::optional<RawImage> decode(const uint8_t* data, size_t size, std::string* outError) {
    BlpHeader header;
    if (!parseHeader(data, size, header, outError)) {
        return std::nullopt;
    }

    // 从最大级开始逐级尝试，容忍个别 mip 数据损坏的文件
    std::string firstError;
    bool tried = false;
    for (uint32_t level = 0; level < MAX_MIPS; ++level) {
        if (header.mipmapOffsets[level] == 0 || header.mipmapSizes[level] == 0) continue;
        std::string levelError;
        auto image = decodeMip(data, size, level, &levelError);
        if (image) return image;
        if (!tried) {
            firstError = levelError;
            tried = true;
        }
    }

    if (!tried) {
        // 没有任何有效 mip 条目，按 0 级强行尝试一次以拿到具体错误
        return decodeMip(data, size, 0, outError);
    }
    setError(outError, firstError.empty() ? "BLP 解码失败" : firstError);
    return std::nullopt;
}

bool encodeJpegBlp1(const uint8_t* rgba, uint32_t width, uint32_t height,
                    int quality, int mipCount,
                    std::vector<uint8_t>& outBlp, std::string* outError) {
    outBlp.clear();

    if (!rgba || width == 0 || height == 0) {
        setError(outError, "无效的图像数据");
        return false;
    }

    // 完整 mip 链级数（到 1x1 为止），再按调用方要求截断
    int fullLevels = 1;
    while ((std::max(width, height) >> fullLevels) >= 1 && fullLevels < MAX_MIPS) {
        ++fullLevels;
    }
    const int levels = std::clamp(mipCount, 1, fullLevels);
    const int tjQuality = std::clamp(quality, 1, 100);

    tjhandle tjHandle = tj3Init(TJINIT_COMPRESS);
    if (!tjHandle) {
        setError(outError, "turbojpeg 初始化失败");
        return false;
    }
    // 编码必须显式指定 TJCS_CMYK，这才是 War3 BLP1 JPEG-content 的正确布局：
    // 4 个 JPEG 分量只作为 B/G/R/A 原始通道容器使用，不做 RGB<->CMYK 色彩转换。
    // 如果使用旧 tjCompress2(TJPF_CMYK) 默认路径，libjpeg-turbo 会写成 Adobe/YCCK，
    // 游戏读取时会出现明显偏色。TJSAMP_444 保持四个通道逐像素对应。
    if (tj3Set(tjHandle, TJPARAM_QUALITY, tjQuality) != 0 ||
        tj3Set(tjHandle, TJPARAM_SUBSAMP, TJSAMP_444) != 0 ||
        tj3Set(tjHandle, TJPARAM_COLORSPACE, TJCS_CMYK) != 0 ||
        tj3Set(tjHandle, TJPARAM_FASTDCT, 0) != 0) {
        setError(outError, std::string("turbojpeg 参数设置失败: ") + tj3GetErrorStr(tjHandle));
        tj3Destroy(tjHandle);
        return false;
    }

    std::vector<std::vector<uint8_t>> mipJpegs;
    mipJpegs.reserve(levels);
    std::vector<uint8_t> levelRgba;
    std::vector<uint8_t> bgra;

    bool ok = true;
    for (int level = 0; level < levels && ok; ++level) {
        const uint32_t mipW = std::max(1u, width >> level);
        const uint32_t mipH = std::max(1u, height >> level);
        const size_t pixelCount = static_cast<size_t>(mipW) * mipH;

        const uint8_t* src = rgba;
        if (level > 0) {
            // 每级都从原图缩放，避免逐级缩放的误差累积；
            // STBIR_RGBA 走 alpha 加权路径，防止透明边缘渗色
            levelRgba.resize(pixelCount * 4);
            if (!stbir_resize_uint8_srgb(rgba, static_cast<int>(width), static_cast<int>(height), 0,
                                         levelRgba.data(), static_cast<int>(mipW), static_cast<int>(mipH), 0,
                                         STBIR_RGBA)) {
                setError(outError, "mipmap 缩放失败");
                ok = false;
                break;
            }
            src = levelRgba.data();
        }

        // 标准 War3 BLP1 写法：RGBA 内存图转换为 JPEG 的 B/G/R/A 四分量。
        // 这里不预乘 alpha、不反相；alpha 就是第 4 个分量。
        bgra.resize(pixelCount * 4);
        for (size_t i = 0; i < pixelCount; ++i) {
            bgra[i * 4 + 0] = src[i * 4 + 2];
            bgra[i * 4 + 1] = src[i * 4 + 1];
            bgra[i * 4 + 2] = src[i * 4 + 0];
            bgra[i * 4 + 3] = src[i * 4 + 3];
        }

        unsigned char* jpegBuf = nullptr;
        size_t jpegSize = 0;
        if (tj3Compress8(tjHandle, bgra.data(), static_cast<int>(mipW), 0, static_cast<int>(mipH),
                         TJPF_CMYK, &jpegBuf, &jpegSize) != 0) {
            setError(outError, std::string("turbojpeg 编码失败: ") + tj3GetErrorStr(tjHandle));
            ok = false;
        } else {
            mipJpegs.emplace_back(jpegBuf, jpegBuf + jpegSize);
        }
        if (jpegBuf) tj3Free(jpegBuf);
    }
    tj3Destroy(tjHandle);
    if (!ok) return false;

    // 组装文件：156 字节头 + jpegHeaderSize(=0) + 逐 mip 完整 JPEG
    size_t totalSize = HEADER_SIZE + 4;
    for (const auto& jpeg : mipJpegs) totalSize += jpeg.size();
    outBlp.resize(totalSize, 0);

    writeU32(outBlp, 0, MAGIC_BLP1);
    writeU32(outBlp, 4, COMPRESSION_JPEG);
    writeU32(outBlp, 8, 8); // alphaBits
    writeU32(outBlp, 12, width);
    writeU32(outBlp, 16, height);
    writeU32(outBlp, 20, 4); // extra（与暴雪原版文件一致）
    writeU32(outBlp, 24, levels > 1 ? 1u : 0u); // hasMipmaps

    size_t cursor = HEADER_SIZE;
    writeU32(outBlp, cursor, 0); // jpegHeaderSize = 0：每个 mip 自包含完整 JPEG
    cursor += 4;

    for (int level = 0; level < levels; ++level) {
        const auto& jpeg = mipJpegs[level];
        writeU32(outBlp, 28 + level * 4, static_cast<uint32_t>(cursor));
        writeU32(outBlp, 28 + 64 + level * 4, static_cast<uint32_t>(jpeg.size()));
        std::memcpy(outBlp.data() + cursor, jpeg.data(), jpeg.size());
        cursor += jpeg.size();
    }

    return true;
}

} // namespace blpcodec
