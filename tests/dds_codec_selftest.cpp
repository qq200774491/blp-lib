#include "core/dds_codec.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

void put_u32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset + 0] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

std::vector<uint8_t> compressed_dds(uint32_t format, const std::vector<uint8_t>& block) {
    std::vector<uint8_t> bytes(128 + block.size(), 0);
    std::memcpy(bytes.data(), "DDS ", 4);
    put_u32(bytes, 4, 124);
    put_u32(bytes, 8, 0x00081007);
    put_u32(bytes, 12, 4);
    put_u32(bytes, 16, 4);
    put_u32(bytes, 20, static_cast<uint32_t>(block.size()));
    put_u32(bytes, 76, 32);
    put_u32(bytes, 80, 4);
    put_u32(bytes, 84, format);
    put_u32(bytes, 108, 0x1000);
    std::copy(block.begin(), block.end(), bytes.begin() + 128);
    return bytes;
}

bool expect_pixel(const ddscodec::RawImage& image, size_t pixel,
                  std::array<uint8_t, 4> expected, const char* label) {
    const uint8_t* actual = image.rgba.data() + pixel * 4;
    if (!std::equal(expected.begin(), expected.end(), actual)) {
        std::printf("[FAIL] %s: got %u,%u,%u,%u\n", label,
                    actual[0], actual[1], actual[2], actual[3]);
        return false;
    }
    return true;
}

} // namespace

int main() {
    int failures = 0;
    std::string error;

    const std::array<uint8_t, 24> original = {
        1, 2, 3, 4,       20, 30, 40, 50,    60, 70, 80, 90,
        100, 110, 120, 130, 140, 150, 160, 170, 200, 210, 220, 230,
    };
    std::vector<uint8_t> encoded;
    if (!ddscodec::encode_bgra8(original.data(), 3, 2, encoded, &error)) {
        std::printf("[FAIL] BGRA8 encode: %s\n", error.c_str());
        ++failures;
    } else {
        auto decoded = ddscodec::decode(encoded.data(), encoded.size(), &error);
        if (!decoded || decoded->width != 3 || decoded->height != 2 ||
            decoded->rgba.size() != original.size() ||
            !std::equal(original.begin(), original.end(), decoded->rgba.begin())) {
            std::printf("[FAIL] BGRA8 round trip: %s\n", error.c_str());
            ++failures;
        }
    }

    // DXT1 block: red endpoint selected by every pixel.
    std::vector<uint8_t> dxt1 = {0x00, 0xf8, 0xe0, 0x07, 0, 0, 0, 0};
    auto dxt1File = compressed_dds(fourcc('D', 'X', 'T', '1'), dxt1);
    auto dxt1Image = ddscodec::decode(dxt1File.data(), dxt1File.size(), &error);
    if (!dxt1Image || !expect_pixel(*dxt1Image, 0, {255, 0, 0, 255}, "DXT1")) ++failures;

    // DXT5 block: opaque alpha and green color.
    std::vector<uint8_t> dxt5(16, 0);
    dxt5[0] = 255;
    dxt5[1] = 0;
    dxt5[8] = 0xe0;
    dxt5[9] = 0x07;
    auto dxt5File = compressed_dds(fourcc('D', 'X', 'T', '5'), dxt5);
    auto dxt5Image = ddscodec::decode(dxt5File.data(), dxt5File.size(), &error);
    if (!dxt5Image || !expect_pixel(*dxt5Image, 0, {0, 255, 0, 255}, "DXT5")) ++failures;

    // ATI2/BC5 block: constant red=255, green=128.
    std::vector<uint8_t> bc5(16, 0);
    bc5[0] = bc5[1] = 255;
    bc5[8] = bc5[9] = 128;
    auto bc5File = compressed_dds(fourcc('A', 'T', 'I', '2'), bc5);
    auto bc5Image = ddscodec::decode(bc5File.data(), bc5File.size(), &error);
    if (!bc5Image || !expect_pixel(*bc5Image, 0, {255, 128, 0, 255}, "BC5")) ++failures;

    // DX10 R8 texture: verifies the extended header path and grayscale expansion.
    std::vector<uint8_t> dx10(150, 0);
    std::memcpy(dx10.data(), "DDS ", 4);
    put_u32(dx10, 4, 124);
    put_u32(dx10, 8, 0x00001007);
    put_u32(dx10, 12, 1);
    put_u32(dx10, 16, 2);
    put_u32(dx10, 76, 32);
    put_u32(dx10, 80, 4);
    put_u32(dx10, 84, fourcc('D', 'X', '1', '0'));
    put_u32(dx10, 108, 0x1000);
    put_u32(dx10, 128, 61); // DXGI_FORMAT_R8_UNORM
    put_u32(dx10, 132, 3);  // D3D10_RESOURCE_DIMENSION_TEXTURE2D
    put_u32(dx10, 140, 1);  // arraySize
    dx10[148] = 7;
    dx10[149] = 200;
    auto dx10Image = ddscodec::decode(dx10.data(), dx10.size(), &error);
    if (!dx10Image || !expect_pixel(*dx10Image, 0, {7, 7, 7, 255}, "DX10 R8 first") ||
        !expect_pixel(*dx10Image, 1, {200, 200, 200, 255}, "DX10 R8 second")) {
        ++failures;
    }

    auto truncated = dxt5File;
    truncated.pop_back();
    if (ddscodec::decode(truncated.data(), truncated.size(), &error)) {
        std::printf("[FAIL] truncated DDS unexpectedly decoded\n");
        ++failures;
    }

    if (failures == 0) std::printf("[PASS] DDS codec self-test\n");
    return failures == 0 ? 0 : 1;
}
