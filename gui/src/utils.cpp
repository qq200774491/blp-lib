#include "utils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

std::filesystem::path fsPathFromUtf8(const std::string& utf8Path) {
    try {
        return std::filesystem::u8path(utf8Path);
    } catch (...) {
        return std::filesystem::path(utf8Path);
    }
}

std::string fsPathToUtf8(const std::filesystem::path& path) {
    try {
        return path.u8string();
    } catch (...) {
        return path.string();
    }
}

bool isPowerOfTwo(int value) {
    return value > 0 && ((value & (value - 1)) == 0);
}

int nearestPowerOfTwo(int value) {
    if (value <= 0) return 1;
    if (isPowerOfTwo(value)) return value;

    int upper = 1;
    while (upper < value && upper < (1 << 30)) upper <<= 1;
    int lower = upper >> 1;
    if (value - lower < upper - value) return lower;
    if (value - lower > upper - value) return upper;
    return upper;
}

int nextPowerOfTwo(int value) {
    if (value <= 0) return 1;
    int upper = 1;
    while (upper < value && upper < (1 << 30)) upper <<= 1;
    return upper;
}

int autoMipCount(int width, int height) {
    int maxDim = (width > height) ? width : height;
    int count = 1;
    while (maxDim > 1 && count < 16) {
        maxDim /= 2;
        ++count;
    }
    return count;
}

std::string formatFileSize(uint64_t bytes) {
    char buf[64];
    const double kb = 1024.0;
    const double mb = kb * 1024.0;
    const double gb = mb * 1024.0;

    if (bytes >= static_cast<uint64_t>(gb)) {
        std::snprintf(buf, sizeof(buf), "%.2f GB", bytes / gb);
    } else if (bytes >= static_cast<uint64_t>(mb)) {
        std::snprintf(buf, sizeof(buf), "%.2f MB", bytes / mb);
    } else if (bytes >= static_cast<uint64_t>(kb)) {
        std::snprintf(buf, sizeof(buf), "%.2f KB", bytes / kb);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return buf;
}

std::string normalizeFormat(const std::string& format) {
    std::string fmt = format;
    // Trim
    while (!fmt.empty() && fmt.front() == ' ') fmt.erase(fmt.begin());
    while (!fmt.empty() && fmt.back() == ' ') fmt.pop_back();
    // Lowercase
    std::transform(fmt.begin(), fmt.end(), fmt.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (fmt == "jpeg") return "jpg";
    return fmt;
}

std::vector<std::string> supportedExtensions() {
    return {"blp", "png", "jpg", "jpeg", "bmp", "tga"};
}

bool isSupportedFile(const std::string& path) {
    namespace fs = std::filesystem;
    std::string ext = fsPathFromUtf8(path).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::string normalized = normalizeFormat(ext);
    for (const auto& supported : supportedExtensions()) {
        if (normalized == supported) return true;
    }
    return false;
}
