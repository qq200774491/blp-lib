#pragma once

#include <string>
#include <vector>
#include <cstdint>

bool isPowerOfTwo(int value);
int nearestPowerOfTwo(int value);
int nextPowerOfTwo(int value);
int autoMipCount(int width, int height);
int snapResizeValue(int value);

std::string normalizeInputPath(const std::string& raw);
std::string formatFileSize(uint64_t bytes);
std::string normalizeFormat(const std::string& format);
std::vector<std::string> supportedExtensions();
bool isSupportedFile(const std::string& path);
