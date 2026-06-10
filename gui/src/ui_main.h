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
#include "thumbnail_cache.h"

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
    bool recursive = true;
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

    // Preview-toolbar target-size mode (preview save buttons only).
    // 0 = 默认尺寸 (use resizeWidth/resizeHeight), 1 = 64x64, 2 = 128x128
    int targetSizeMode = 0;

    // Batch output size (shared by 批量转换 + 图层合成). Kept separate from the
    // preview resize controls because updatePreview overwrites those per file.
    int batchSizeMode = 0;      // 0=原始大小 1=64×64 2=128×128 3=自定义
    int batchWidth = 256;
    int batchHeight = 256;
    bool batchLockAspect = true;
    float batchAspect = 1.0f;   // W/H captured when 锁比例 turns on (not persisted)
    int batchResizeMethod = 1;  // 0=拉伸(Stretch) 1=居中透明(CenterTransparent)

    // Right panel view mode + thumbnail grid cache. Auto-switched: single-file
    // open/click -> 预览, multi-file/folder add -> 网格.
    int rightViewMode = 1;      // 0=网格 1=预览
    ThumbnailCache thumbCache;

    // Layer compositing / border batch settings
    std::string overlayImagePath;          // overlay image (utf8)
    int overlayAnchor = 4;                 // Anchor9 index, 4 = Center
    int overlayMarginPx = 0;
    int overlayScalePct = 100;
    float overlayOpacity = 1.0f;
    int borderThicknessPx = 4;
    float borderColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // RGBA, alpha editable
    bool borderExpandCanvas = false;       // false = draw inside, true = expand canvas
    int composeOpMode = 0;                 // 0 = 图层合成, 1 = 边框

    // BLP API
    BlpApi blpApi;

    // Zoom
    int zoomPercent = 100;

    // Association status
    bool blpAssociated = false;
    bool pngAssociated = false;
    bool tgaAssociated = false;
    bool thumbnailRegistered = false;

    // DPI scale
    float dpiScale = 1.0f;
};

void renderMainUI(AppState& state);
void renderMenuBar(AppState& state);
void renderStatusBar(AppState& state);
