#pragma once

#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <atomic>
#include <cstdint>

#include "imgui.h"
#include "blp_api.h"
#include "image_io.h"
#include "image_view.h"
#include "settings.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

struct BlpMipEntry {
    int index = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct ConvertItemResult {
    std::string inputPath;
    std::string outputPath;
    bool success = false;
    std::string error;
};

struct AppState {
    // Window handle
    HWND hwnd = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* deviceContext = nullptr;

    // Settings
    AppSettings settings;

    // Left panel width (splitter)
    float leftPanelWidth = 320.0f;

    // File list
    std::vector<std::string> fileList;
    std::set<std::string> fileSet;
    std::unordered_map<std::string, std::string> relativePathMap;
    int selectedFileIndex = -1;

    // Convert settings
    int outputFormat = 0;  // 0=BLP 1=PNG 2=JPG 3=BMP 4=TGA
    int quality = 100;
    bool overwrite = false;
    bool recursive = false;
    char inputDirBuf[1024] = {};
    char outputDirBuf[1024] = {};

    // Conversion progress
    std::atomic<float> convertProgress{0.0f};
    std::atomic<bool> converting{false};
    std::vector<std::string> logMessages;
    std::vector<ConvertItemResult> lastConvertResults;
    int lastConvertTotal = 0;
    int lastConvertSuccess = 0;
    int lastConvertFailed = 0;
    bool taskDrawerVisible = false;
    float taskDrawerHeight = 170.0f;
    bool showAboutPopup = false;

    // Preview state
    ImageViewer imageViewer;
    std::string currentPreviewPath;
    std::vector<uint8_t> currentBlpBytes;
    std::vector<uint8_t> originalFileBytes;
    ImageMeta currentMeta;
    ImageMeta originalMeta;
    bool currentIsBlp = false;
    int currentMipIndex = 0;
    bool hasOriginalBackup = false;
    bool previewAdjusted = false;
    bool previewUseAlpha = false;

    // RGBA data (CPU side)
    std::vector<uint8_t> previewOriginalRGBA;
    std::vector<uint8_t> previewAdjustedRGBA;
    int previewOrigW = 0;
    int previewOrigH = 0;
    int previewAdjW = 0;
    int previewAdjH = 0;

    // Mip entries
    std::vector<BlpMipEntry> mipEntries;
    int selectedMipIndex = 0;

    // Resize controls
    int resizeWidth = 0;
    int resizeHeight = 0;
    bool resizeLockAspect = true;
    bool resizeAspectSyncing = false;
    float previewCanvasBg[3] = {0.878f, 0.894f, 0.922f};

    // BLP API
    BlpApi blpApi;

    // Zoom
    int zoomPercent = 100;

    // Association status
    bool blpAssociated = false;
    bool thumbnailRegistered = false;

    // DPI scale
    float dpiScale = 1.0f;
};

void renderMainUI(AppState& state);
void renderMenuBar(AppState& state);
void renderTaskDrawer(AppState& state);
void renderStatusBar(AppState& state);
