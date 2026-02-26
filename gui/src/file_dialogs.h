#pragma once

#include <string>
#include <vector>

#include <Windows.h>

std::vector<std::wstring> openFileDialog(HWND hwnd, const wchar_t* filter, bool multiSelect = false);
std::wstring openFolderDialog(HWND hwnd);
std::wstring saveFileDialog(HWND hwnd, const wchar_t* filter, const wchar_t* defaultExt);
