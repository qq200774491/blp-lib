#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

bool isPowerOfTwo(int value);
int nearestPowerOfTwo(int value);
int nextPowerOfTwo(int value);
int autoMipCount(int width, int height);

std::filesystem::path fsPathFromUtf8(const std::string& utf8Path);
std::string fsPathToUtf8(const std::filesystem::path& path);

std::string formatFileSize(uint64_t bytes);
std::string normalizeFormat(const std::string& format);
std::vector<std::string> supportedExtensions();
bool isSupportedFile(const std::string& path);
