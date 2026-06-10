#pragma once

#include <string>
#include <vector>

#include "blp/blp_codec.h"

// 内置 BLP 编解码门面。
// 历史上这里是 blp_lib.dll（Rust）的动态加载封装；现在编解码直接编译进程序
// （见 src/blp/blp_codec.h），此类仅保留状态/版本查询与解码入口，避免大改调用方。
class BlpApi {
public:
    bool ensureLoaded(std::string* outError) const { (void)outError; return true; }
    bool isLoaded() const { return true; }
    std::string libraryPath() const { return "built-in"; }
    std::string version() const;

    // BLP 字节 → RGBA（带 mip 回退容错）
    bool loadFromBuffer(const std::vector<uint8_t>& data,
                        blpcodec::RawImage* outImage,
                        std::string* outError = nullptr) const;
};
