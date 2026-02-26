#pragma once

#include <string>

#include <Windows.h>

bool isBlpAssociated();
bool registerBlpAssociation(const std::wstring& appPath, std::string* outError);
bool isThumbnailRegistered();
bool callDllEntry(const std::wstring& dllPath, const char* entry, std::string* outError);

std::wstring getAppDir();
std::wstring getAppPath();
