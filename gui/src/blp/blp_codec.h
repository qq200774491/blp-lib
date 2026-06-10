// 自包含的 BLP1 编解码器（解码移植自 ShadowEngine BlpDecoder，编码为新实现）。
// 仅支持 War3 的 BLP1：调色板（alpha 0/1/4/8 bit）与 JPEG-content 两种变体。
// BLP2 会被显式拒绝。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace blpcodec {

struct RawImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba; // width * height * 4
};

// 数据是否为 BLP1（只看魔数）
bool isBlp1(const uint8_t* data, size_t size);

// 数据是否为任意 BLP（BLP1/BLP2 魔数，用于文件类型识别）
bool isBlpMagic(const uint8_t* data, size_t size);

// 解码指定 mip 级别（0 = 最大）。失败返回 nullopt，outError 可为空。
std::optional<RawImage> decodeMip(const uint8_t* data, size_t size,
                                  uint32_t mipLevel, std::string* outError);

// 带回退的解码：从最大级开始枚举有效 mip，返回第一个解码成功的级别。
// 容忍顶层 mip 数据损坏的文件（与旧 Rust blp_lib 行为一致）。
std::optional<RawImage> decode(const uint8_t* data, size_t size, std::string* outError);

// RGBA 裸数据编码为 JPEG-content BLP1 字节流。
// quality 0-100（内部钳位到 1-100），mipCount >= 1（16 = 完整 mip 链）。
bool encodeJpegBlp1(const uint8_t* rgba, uint32_t width, uint32_t height,
                    int quality, int mipCount,
                    std::vector<uint8_t>& outBlp, std::string* outError);

} // namespace blpcodec
