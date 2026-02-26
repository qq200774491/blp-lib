#include "ui_right_panel.h"
#include "ui_main.h"
#include "style.h"
#include "utils.h"
#include "win_integration.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_RESIZE2_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace {

std::vector<BlpMipEntry> readBlpMipEntries(const std::vector<uint8_t>& bytes) {
    std::vector<BlpMipEntry> entries;
    if (bytes.size() < 148) return entries;

    const uint8_t* data = bytes.data();
    bool isBlp1 = (memcmp(data, "BLP1", 4) == 0);
    bool isBlp2 = (memcmp(data, "BLP2", 4) == 0);
    if (!isBlp1 && !isBlp2) return entries;

    int headerSize = isBlp1 ? 156 : 148;
    if (static_cast<int>(bytes.size()) < headerSize) return entries;

    int offsetsOffset = isBlp1 ? 28 : 20;
    int sizesOffset = offsetsOffset + 16 * 4;

    for (int i = 0; i < 16; ++i) {
        uint32_t offset = 0, size = 0;
        memcpy(&offset, data + offsetsOffset + i * 4, 4);
        memcpy(&size, data + sizesOffset + i * 4, 4);
        if (offset != 0 && size != 0) {
            entries.push_back({i, offset, size});
        }
    }

    return entries;
}

bool writeFileBytes(const std::string& path, const std::vector<uint8_t>& bytes, std::string* outError) {
    std::ofstream file(fsPathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        if (outError) *outError = "写入文件失败";
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!file.good()) {
        if (outError) *outError = "写入文件不完整";
        return false;
    }
    return true;
}

void logMsg(AppState& state, const std::string& msg) {
    state.logMessages.push_back(msg);
}

void updatePreview(AppState& state, const std::string& path) {
    // Clear state
    state.currentPreviewPath = path;
    state.currentBlpBytes.clear();
    state.originalFileBytes.clear();
    state.currentMeta = ImageMeta();
    state.originalMeta = ImageMeta();
    state.currentIsBlp = false;
    state.currentMipIndex = 0;
    state.hasOriginalBackup = false;
    state.previewAdjusted = false;
    state.previewOriginalRGBA.clear();
    state.previewAdjustedRGBA.clear();
    state.mipEntries.clear();
    state.selectedMipIndex = 0;

    namespace fs = std::filesystem;
    const fs::path fsInputPath = fsPathFromUtf8(path);
    std::string ext = fsInputPath.extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    ext = normalizeFormat(ext);

    // Read file bytes
    std::ifstream file(fsInputPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        logMsg(state, "打开失败：" + path);
        state.imageViewer.clearImage();
        return;
    }
    auto fileSize = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(bytes.data()), fileSize);
    file.close();

    if (bytes.empty()) {
        logMsg(state, "文件为空：" + path);
        state.imageViewer.clearImage();
        return;
    }

    state.originalFileBytes = bytes;

    RgbaImage image;
    ImageMeta meta;
    std::string error;

    if (ext == "blp") {
        state.currentIsBlp = true;
        state.currentBlpBytes = bytes;

        if (!state.blpApi.ensureLoaded(&error)) {
            logMsg(state, "BLP 未加载：" + error);
            state.imageViewer.clearImage();
            return;
        }

        BlpImage blpImage = {};
        BlpResult result = state.blpApi.loadFromBuffer(bytes, &blpImage);
        if (result != BLP_SUCCESS) {
            logMsg(state, "BLP 解码失败：" + path);
            state.imageViewer.clearImage();
            return;
        }

        image.width = static_cast<int>(blpImage.width);
        image.height = static_cast<int>(blpImage.height);
        image.pixels.assign(blpImage.data, blpImage.data + blpImage.data_len);
        state.blpApi.freeImage(&blpImage);

        meta.width = image.width;
        meta.height = image.height;
        meta.format = "blp";
        meta.fileSize = static_cast<uint64_t>(fileSize);

        state.mipEntries = readBlpMipEntries(bytes);
    } else {
        if (!loadImageFile(path, &image, &meta, &error, &state.blpApi)) {
            logMsg(state, "预览失败：" + path + "（" + error + "）");
            state.imageViewer.clearImage();
            return;
        }
    }

    state.currentMeta = meta;
    state.originalMeta = meta;
    state.hasOriginalBackup = !state.originalFileBytes.empty();
    state.previewOriginalRGBA = image.pixels;
    state.previewOrigW = image.width;
    state.previewOrigH = image.height;

    // Apply alpha if BLP and not showing alpha
    std::vector<uint8_t> displayPixels = image.pixels;
    if (state.currentIsBlp && !state.previewUseAlpha) {
        for (size_t i = 3; i < displayPixels.size(); i += 4) {
            displayPixels[i] = 255;
        }
    }

    state.imageViewer.setImage(state.device, displayPixels.data(), image.width, image.height);

    // Update resize controls
    state.resizeWidth = image.width;
    state.resizeHeight = image.height;
}

void refreshPreviewDisplay(AppState& state) {
    const auto& rgba = (state.previewAdjusted && !state.previewAdjustedRGBA.empty())
                           ? state.previewAdjustedRGBA
                           : state.previewOriginalRGBA;
    int w = state.previewAdjusted ? state.previewAdjW : state.previewOrigW;
    int h = state.previewAdjusted ? state.previewAdjH : state.previewOrigH;

    if (rgba.empty()) return;

    std::vector<uint8_t> displayPixels = rgba;
    if (state.currentIsBlp && !state.previewUseAlpha) {
        for (size_t i = 3; i < displayPixels.size(); i += 4) {
            displayPixels[i] = 255;
        }
    }

    state.imageViewer.setImage(state.device, displayPixels.data(), w, h);
}

bool saveAlignedToSource(AppState& state, const std::vector<uint8_t>& rgba,
                         int w, int h, std::string* outError) {
    if (state.currentPreviewPath.empty()) {
        if (outError) *outError = "未选择文件";
        return false;
    }

    namespace fs = std::filesystem;
    std::string ext = fsPathFromUtf8(state.currentPreviewPath).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::string format = normalizeFormat(ext);

    RgbaImage img;
    img.width = w;
    img.height = h;
    img.pixels = rgba;

    int mipCount = (format == "blp") ? autoMipCount(w, h) : 1;
    if (!writeImageFile(state.currentPreviewPath, format, img, state.quality, mipCount, outError, &state.blpApi)) {
        return false;
    }

    // Update state
    state.currentMeta.width = w;
    state.currentMeta.height = h;
    state.currentMeta.format = format;
    try {
        state.currentMeta.fileSize = static_cast<uint64_t>(fs::file_size(fsPathFromUtf8(state.currentPreviewPath)));
    } catch (...) {}

    state.currentIsBlp = (format == "blp");
    state.currentMipIndex = 0;

    if (state.currentIsBlp) {
        std::ifstream f(fsPathFromUtf8(state.currentPreviewPath), std::ios::binary | std::ios::ate);
        if (f.is_open()) {
            auto sz = f.tellg();
            f.seekg(0);
            state.currentBlpBytes.resize(static_cast<size_t>(sz));
            f.read(reinterpret_cast<char*>(state.currentBlpBytes.data()), sz);
            state.mipEntries = readBlpMipEntries(state.currentBlpBytes);
        }
    } else {
        state.currentBlpBytes.clear();
        state.mipEntries.clear();
    }

    state.previewAdjustedRGBA = rgba;
    state.previewAdjW = w;
    state.previewAdjH = h;
    state.previewAdjusted = true;
    state.resizeWidth = w;
    state.resizeHeight = h;

    refreshPreviewDisplay(state);
    return true;
}

} // namespace

void renderRightPanel(AppState& state) {
    // Check if selected file changed
    static int lastSelectedIndex = -2;
    if (state.selectedFileIndex != lastSelectedIndex) {
        lastSelectedIndex = state.selectedFileIndex;
        if (state.selectedFileIndex >= 0 && state.selectedFileIndex < static_cast<int>(state.fileList.size())) {
            updatePreview(state, state.fileList[state.selectedFileIndex]);
        } else {
            state.imageViewer.clearImage();
            state.currentPreviewPath.clear();
            state.currentMeta = ImageMeta();
        }
    }

    const bool hasImage = state.currentMeta.width > 0;
    const bool isPot = isPowerOfTwo(state.currentMeta.width) && isPowerOfTwo(state.currentMeta.height);

    auto getActiveSource = [&](const std::vector<uint8_t>** outPixels, int* outW, int* outH) -> bool {
        if (!outPixels || !outW || !outH) return false;
        if (state.previewAdjusted && !state.previewAdjustedRGBA.empty()) {
            *outPixels = &state.previewAdjustedRGBA;
            *outW = state.previewAdjW;
            *outH = state.previewAdjH;
        } else {
            *outPixels = &state.previewOriginalRGBA;
            *outW = state.previewOrigW;
            *outH = state.previewOrigH;
        }
        return *outPixels && !(*outPixels)->empty() && *outW > 0 && *outH > 0;
    };

    auto resizeAndSave = [&](int targetW, int targetH, const char* successPrefix) {
        const std::vector<uint8_t>* source = nullptr;
        int srcW = 0;
        int srcH = 0;
        if (!getActiveSource(&source, &srcW, &srcH)) return;

        if (srcW == targetW && srcH == targetH) {
            logMsg(state, "尺寸未变化，无需保存");
            return;
        }

        std::vector<uint8_t> resized(static_cast<size_t>(targetW) * targetH * 4);
        stbir_resize_uint8_linear(source->data(), srcW, srcH, srcW * 4,
                                  resized.data(), targetW, targetH, targetW * 4, STBIR_RGBA);
        std::string error;
        if (saveAlignedToSource(state, resized, targetW, targetH, &error)) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "%s：%d x %d", successPrefix, targetW, targetH);
            logMsg(state, msg);
        } else {
            logMsg(state, "调整尺寸失败：" + error);
        }
    };

    // Info bar
    {
        if (!isPot && hasImage) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 0.941f, 0.941f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.933f, 0.949f, 0.973f, 1.0f));
        }

        ImGui::BeginChild("InfoBar", ImVec2(-1, 36 * state.dpiScale), ImGuiChildFlags_Borders);

        if (hasImage) {
            char info[256];
            std::snprintf(info, sizeof(info), "%d x %d 像素 | %s | %s",
                          state.currentMeta.width, state.currentMeta.height,
                          formatFileSize(state.currentMeta.fileSize).c_str(),
                          state.currentMeta.format.c_str());

            std::string infoStr = info;
            if (state.currentIsBlp && state.currentMipIndex >= 0) {
                char mipBuf[32];
                std::snprintf(mipBuf, sizeof(mipBuf), " | 层级 %d", state.currentMipIndex + 1);
                infoStr += mipBuf;
            }
            if (!isPot) {
                infoStr += " | 非 2 次幂";
            }

            if (!isPot) {
                ImGui::PushStyleColor(ImGuiCol_Text, UiColorWarning());
            }
            ImGui::TextUnformatted(infoStr.c_str());
            if (!isPot) {
                ImGui::PopStyleColor();
            }
        } else {
            ImGui::TextUnformatted("未加载图像");
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // Image preview area
    {
        float remainingH = ImGui::GetContentRegionAvail().y;
        float previewH = remainingH;
        if (hasImage) {
            float inspectorReserve = 250.0f * state.dpiScale;
            if (state.currentIsBlp && !state.mipEntries.empty()) {
                inspectorReserve += 60.0f * state.dpiScale;
            }
            if (remainingH <= 560.0f * state.dpiScale) {
                inspectorReserve += 40.0f * state.dpiScale;
            }
            previewH -= inspectorReserve;
        }
        const float minPreviewH = (remainingH <= 560.0f * state.dpiScale)
            ? (160.0f * state.dpiScale)
            : (200.0f * state.dpiScale);
        previewH = std::max(previewH, minPreviewH);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.878f, 0.894f, 0.922f, 1.0f));
        ImGui::BeginChild("PreviewArea", ImVec2(-1, previewH), ImGuiChildFlags_Borders);

        ImVec2 avail = ImGui::GetContentRegionAvail();

        // Stage-only preview, controls moved to inspector panels below.
        ImGui::SetCursorPos(ImVec2(0, 0));
        state.imageViewer.render(avail.x, avail.y);

        // Handle resize on fit
        if (state.imageViewer.fitMode && state.imageViewer.hasImage) {
            state.imageViewer.fitToView(avail.x, avail.y);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    if (hasImage && ImGui::CollapsingHeader("预览设置", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state.currentIsBlp) {
            if (ImGui::Checkbox("显示透明通道", &state.previewUseAlpha)) {
                refreshPreviewDisplay(state);
            }
        }

        if (ImGui::Button("背景颜色")) {
            ImGui::OpenPopup("BgColorPicker");
        }
        if (ImGui::BeginPopup("BgColorPicker")) {
            if (ImGui::ColorPicker3("##bgpicker", state.imageViewer.bgColor)) {
                state.imageViewer.useCheckerboard = false;
            }
            if (ImGui::Button("恢复默认")) {
                state.imageViewer.bgColor[0] = 0.878f;
                state.imageViewer.bgColor[1] = 0.894f;
                state.imageViewer.bgColor[2] = 0.922f;
                state.imageViewer.useCheckerboard = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::Button("适应窗口")) {
            state.imageViewer.fitMode = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("原始大小")) {
            state.imageViewer.setZoom(1.0f);
            state.imageViewer.fitMode = false;
        }
        ImGui::SameLine();
        ImGui::PushItemWidth(-1);
        int zoomPct = static_cast<int>(state.imageViewer.zoom * 100.0f + 0.5f);
        zoomPct = std::clamp(zoomPct, 10, 400);
        if (ImGui::SliderInt("##ZoomPreview", &zoomPct, 10, 400, "%d%%")) {
            state.imageViewer.setZoom(zoomPct / 100.0f);
            state.imageViewer.fitMode = false;
        }
        state.zoomPercent = zoomPct;
        ImGui::PopItemWidth();
    }

    // BLP mip inspector
    if (state.currentIsBlp && !state.mipEntries.empty() &&
        ImGui::CollapsingHeader("BLP 层级")) {
        ImGui::BeginChild("MipList", ImVec2(-1, 120 * state.dpiScale), ImGuiChildFlags_Borders);

        for (int i = 0; i < 16; ++i) {
            int mipW = std::max(1, state.previewOrigW >> i);
            int mipH = std::max(1, state.previewOrigH >> i);

            const BlpMipEntry* entry = nullptr;
            for (const auto& e : state.mipEntries) {
                if (e.index == i) { entry = &e; break; }
            }

            bool hasData = (entry && entry->offset != 0 && entry->size != 0) || i == 0;
            char label[128];
            if (hasData) {
                std::string sizeStr = entry ? formatFileSize(entry->size) : "未知大小";
                std::snprintf(label, sizeof(label), "第%d层（%d x %d，%s）",
                              i + 1, mipW, mipH, sizeStr.c_str());
            } else {
                std::snprintf(label, sizeof(label), "第%d层（无数据）", i + 1);
            }

            if (!hasData) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.549f, 0.573f, 0.612f, 1.0f));
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
            } else {
                bool selected = (i == state.selectedMipIndex);
                if (ImGui::Selectable(label, selected)) {
                    if (i != state.currentMipIndex && !state.currentBlpBytes.empty()) {
                        // Decode mip to temp file and load
                        wchar_t tempPath[MAX_PATH] = {};
                        wchar_t tempDir[MAX_PATH] = {};
                        GetTempPathW(MAX_PATH, tempDir);
                        GetTempFileNameW(tempDir, L"blp", 0, tempPath);

                        char narrowPath[MAX_PATH * 3] = {};
                        WideCharToMultiByte(CP_UTF8, 0, tempPath, -1, narrowPath, sizeof(narrowPath), nullptr, nullptr);

                        // Rename to .png
                        std::string pngPath = std::string(narrowPath) + ".png";
                        std::string error;
                        if (state.blpApi.decodeMipToPngFromBuffer(state.currentBlpBytes, i, pngPath, &error)) {
                            RgbaImage mipImage;
                            if (loadImageFile(pngPath, &mipImage, nullptr, &error, &state.blpApi)) {
                                state.previewOriginalRGBA = mipImage.pixels;
                                state.previewOrigW = mipImage.width;
                                state.previewOrigH = mipImage.height;
                                state.previewAdjusted = false;
                                state.previewAdjustedRGBA.clear();
                                state.currentMipIndex = i;
                                state.selectedMipIndex = i;
                                state.resizeWidth = mipImage.width;
                                state.resizeHeight = mipImage.height;
                                refreshPreviewDisplay(state);
                            } else {
                                logMsg(state, "层级预览失败：" + error);
                            }
                        } else {
                            logMsg(state, "层级解码失败：" + error);
                        }

                        // Cleanup temp
                        try { std::filesystem::remove(fsPathFromUtf8(pngPath)); } catch (...) {}
                        try { std::filesystem::remove(fsPathFromUtf8(narrowPath)); } catch (...) {}
                    }
                }
            }
        }

        ImGui::EndChild();
    }

    if (hasImage && ImGui::CollapsingHeader("尺寸调整")) {
        if (ImGui::SliderInt("宽##ResizeWidth", &state.resizeWidth, 1, 16384)) {
            state.resizeWidth = snapResizeValue(state.resizeWidth);
            if (state.resizeLockAspect && state.previewOrigW > 0 && state.previewOrigH > 0) {
                state.resizeAspectSyncing = true;
                state.resizeHeight = std::max(1,
                    static_cast<int>((static_cast<int64_t>(state.resizeWidth) * state.previewOrigH + state.previewOrigW / 2) / state.previewOrigW));
                state.resizeAspectSyncing = false;
            }
        }
        if (ImGui::SliderInt("高##ResizeHeight", &state.resizeHeight, 1, 16384)) {
            state.resizeHeight = snapResizeValue(state.resizeHeight);
            if (state.resizeLockAspect && state.previewOrigW > 0 && state.previewOrigH > 0) {
                state.resizeAspectSyncing = true;
                state.resizeWidth = std::max(1,
                    static_cast<int>((static_cast<int64_t>(state.resizeHeight) * state.previewOrigW + state.previewOrigH / 2) / state.previewOrigH));
                state.resizeAspectSyncing = false;
            }
        }

        ImGui::Checkbox("锁定宽高比", &state.resizeLockAspect);
        if (ImGui::Button("64 x 64")) {
            state.resizeWidth = 64;
            state.resizeHeight = 64;
            resizeAndSave(64, 64, "已调整图像尺寸并保存");
        }
        ImGui::SameLine();
        if (ImGui::Button("128 x 128")) {
            state.resizeWidth = 128;
            state.resizeHeight = 128;
            resizeAndSave(128, 128, "已调整图像尺寸并保存");
        }
        ImGui::SameLine();
        if (ImGui::Button("256 x 256")) {
            state.resizeWidth = 256;
            state.resizeHeight = 256;
            resizeAndSave(256, 256, "已调整图像尺寸并保存");
        }

        PushPrimaryButtonStyle();
        if (ImGui::Button("按当前尺寸保存", ImVec2(-1, 0))) {
            resizeAndSave(std::max(1, state.resizeWidth), std::max(1, state.resizeHeight), "已调整图像尺寸并保存");
        }
        PopButtonStyle();
    }

    if (hasImage && ImGui::CollapsingHeader("对齐与恢复")) {
        const bool originalIsPot = isPowerOfTwo(state.previewOrigW) && isPowerOfTwo(state.previewOrigH);
        const bool canAlign = !originalIsPot && !state.previewAdjusted;
        if (!canAlign) ImGui::BeginDisabled();
        if (ImGui::Button("拉伸对齐 2 次幂", ImVec2(-1, 0))) {
            int targetW = nearestPowerOfTwo(state.previewOrigW);
            int targetH = nearestPowerOfTwo(state.previewOrigH);
            if (targetW != state.previewOrigW || targetH != state.previewOrigH) {
                std::vector<uint8_t> resized(targetW * targetH * 4);
                stbir_resize_uint8_linear(
                    state.previewOriginalRGBA.data(), state.previewOrigW, state.previewOrigH, state.previewOrigW * 4,
                    resized.data(), targetW, targetH, targetW * 4, STBIR_RGBA);
                std::string error;
                if (saveAlignedToSource(state, resized, targetW, targetH, &error)) {
                    logMsg(state, "已拉伸对齐 2 次幂并保存");
                } else {
                    logMsg(state, "对齐失败：" + error);
                }
            }
        }
        if (ImGui::Button("居中对齐 2 次幂", ImVec2(-1, 0))) {
            int targetW = nextPowerOfTwo(state.previewOrigW);
            int targetH = nextPowerOfTwo(state.previewOrigH);
            if (targetW != state.previewOrigW || targetH != state.previewOrigH) {
                std::vector<uint8_t> centered(targetW * targetH * 4, 0);
                int offX = (targetW - state.previewOrigW) / 2;
                int offY = (targetH - state.previewOrigH) / 2;
                for (int y = 0; y < state.previewOrigH; ++y) {
                    memcpy(centered.data() + ((y + offY) * targetW + offX) * 4,
                           state.previewOriginalRGBA.data() + y * state.previewOrigW * 4,
                           state.previewOrigW * 4);
                }
                std::string error;
                if (saveAlignedToSource(state, centered, targetW, targetH, &error)) {
                    logMsg(state, "已居中对齐 2 次幂并保存");
                } else {
                    logMsg(state, "居中对齐失败：" + error);
                }
            }
        }
        if (!canAlign) ImGui::EndDisabled();

        const bool canRestore = state.previewAdjusted && state.hasOriginalBackup;
        if (!canRestore) ImGui::BeginDisabled();
        PushDangerButtonStyle();
        if (ImGui::Button("恢复原始文件", ImVec2(-1, 0))) {
            std::string error;
            if (writeFileBytes(state.currentPreviewPath, state.originalFileBytes, &error)) {
                state.currentMeta = state.originalMeta;
                state.previewAdjusted = false;
                state.previewAdjustedRGBA.clear();
                updatePreview(state, state.currentPreviewPath);
                logMsg(state, "已恢复原始文件");
            } else {
                logMsg(state, "恢复失败：" + error);
            }
        }
        PopButtonStyle();
        if (!canRestore) ImGui::EndDisabled();
    }
}
