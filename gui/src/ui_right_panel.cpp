#include "ui_right_panel.h"
#include "ui_main.h"
#include "style.h"
#include "utils.h"
#include "image_ops.h"
#include "win_integration.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

// stb_image_resize2 的实现由 src/blp/blp_codec.cpp 提供（两个目标都编译它）
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

        blpcodec::RawImage blpImage;
        if (!state.blpApi.loadFromBuffer(bytes, &blpImage, &error)) {
            logMsg(state, "BLP 解码失败：" + path + (error.empty() ? "" : "（" + error + "）"));
            state.imageViewer.clearImage();
            return;
        }

        image.width = static_cast<int>(blpImage.width);
        image.height = static_cast<int>(blpImage.height);
        image.pixels = std::move(blpImage.rgba);

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
    state.imageViewer.fitMode = true;

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

// Map the shared output-format index (AppState::outputFormat) to an extension.
// Mirrors normalizedFormatStr() in ui_left_panel.cpp, which is file-local there.
const char* singleSaveFormatStr(int formatIndex) {
    static const char* formats[] = {"blp", "png", "jpg", "bmp", "tga"};
    if (formatIndex >= 0 && formatIndex < 5) return formats[formatIndex];
    return "blp";
}

bool saveAlignedToSource(AppState& state, const std::vector<uint8_t>& rgba,
                         int w, int h, std::string* outError) {
    if (state.currentPreviewPath.empty()) {
        if (outError) *outError = "未选择文件";
        return false;
    }

    namespace fs = std::filesystem;
    // Output format follows the shared "输出格式" selection (left panel), so
    // single-image save can convert format, not just overwrite in place.
    std::string format = singleSaveFormatStr(state.outputFormat);

    // Write into the source folder, swapping the extension to the chosen format.
    // Same format  -> overwrites the original in place (previous behavior).
    // Diff. format -> writes a new file alongside it and keeps the original.
    fs::path outFsPath = fsPathFromUtf8(state.currentPreviewPath);
    outFsPath.replace_extension("." + format);
    std::string outPath = fsPathToUtf8(outFsPath);
    const bool formatChanged = (outPath != state.currentPreviewPath);

    RgbaImage img;
    img.width = w;
    img.height = h;
    img.pixels = rgba;

    int mipCount = (format == "blp") ? autoMipCount(w, h) : 1;
    if (!writeImageFile(outPath, format, img, state.quality, mipCount, outError, &state.blpApi)) {
        return false;
    }
    state.thumbCache.invalidate(outPath);

    // Switch the working file to what we just wrote. A format change replaces
    // the source: delete the original file and swap the list entry in place,
    // keeping an undo record for 恢复原图.
    const std::string oldPath = state.currentPreviewPath;
    state.currentPreviewPath = outPath;
    if (formatChanged) {
        state.convertedFromPath = oldPath;
        state.convertedFromBytes = state.originalFileBytes;
        state.convertedToPath = outPath;

        std::error_code removeEc;
        if (!fs::remove(fsPathFromUtf8(oldPath), removeEc) || removeEc) {
            logMsg(state, "原文件删除失败：" + oldPath);
        }
        state.thumbCache.invalidate(oldPath);

        const bool newAlreadyListed = state.fileSet.count(outPath) != 0;
        auto oldIt = std::find(state.fileList.begin(), state.fileList.end(), oldPath);
        if (oldIt != state.fileList.end()) {
            if (newAlreadyListed) {
                state.fileList.erase(oldIt);
            } else {
                *oldIt = outPath;
            }
            state.fileSet.erase(oldPath);
            auto relIt = state.relativePathMap.find(oldPath);
            if (relIt != state.relativePathMap.end()) {
                fs::path relPath = fsPathFromUtf8(relIt->second);
                relPath.replace_extension("." + format);
                state.relativePathMap.erase(relIt);
                state.relativePathMap[outPath] = fsPathToUtf8(relPath);
            }
        } else if (!newAlreadyListed) {
            state.fileList.push_back(outPath);
        }
        if (!newAlreadyListed) {
            state.fileSet.insert(outPath);
        }
        auto it = std::find(state.fileList.begin(), state.fileList.end(), outPath);
        if (it != state.fileList.end()) {
            state.selectedFileIndex = static_cast<int>(it - state.fileList.begin());
        }
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

// Thumbnail-grid view of the whole file list (right panel 网格 mode).
void renderThumbnailGrid(AppState& state) {
    const int thumbPx = static_cast<int>(96.0f * state.dpiScale + 0.5f);
    if (state.thumbCache.generatedThumbPx != thumbPx) {
        state.thumbCache.clear();
        state.thumbCache.generatedThumbPx = thumbPx;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 1));
    ImGui::BeginChild("ThumbGrid", ImVec2(-1, -1), ImGuiChildFlags_Borders);

    if (state.fileList.empty()) {
        ImGui::TextDisabled("列表为空，请从左侧添加文件");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float pad = 6.0f * state.dpiScale;
    const float textH = ImGui::GetTextLineHeightWithSpacing();
    const float cellW = thumbPx + pad * 2.0f;
    const float cellH = thumbPx + pad * 2.0f + textH;
    const float availW = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1,
        static_cast<int>((availW + style.ItemSpacing.x) / (cellW + style.ItemSpacing.x)));
    const int total = static_cast<int>(state.fileList.size());
    const int rows = (total + columns - 1) / columns;

    // Per-frame decode budget keeps the UI responsive with large lists; the
    // clipper limits work (and loading) to visible rows.
    int loadBudget = 3;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGuiListClipper clipper;
    clipper.Begin(rows, cellH + style.ItemSpacing.y);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int col = 0; col < columns; ++col) {
                const int i = row * columns + col;
                if (i >= total) break;
                if (col > 0) ImGui::SameLine();

                const std::string& path = state.fileList[i];
                ImGui::PushID(i);

                const bool selected = (i == state.selectedFileIndex);
                if (ImGui::Selectable("##cell", selected,
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(cellW, cellH))) {
                    state.selectedFileIndex = i;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        state.rightViewMode = 1;
                    }
                }

                ThumbnailEntry* entry = state.thumbCache.find(path);
                if ((!entry || !entry->loaded) && loadBudget > 0) {
                    entry = &state.thumbCache.loadNow(state.device, &state.blpApi, path, thumbPx);
                    --loadBudget;
                }

                if (ImGui::IsItemHovered()) {
                    if (entry && entry->loaded && !entry->failed) {
                        ImGui::SetTooltip("%s\n%d x %d | %s", path.c_str(),
                                          entry->srcW, entry->srcH,
                                          formatFileSize(entry->fileSize).c_str());
                    } else {
                        ImGui::SetTooltip("%s", path.c_str());
                    }
                }

                const ImVec2 p0 = ImGui::GetItemRectMin();
                const ImVec2 thumbMin(p0.x + pad, p0.y + pad);
                const ImVec2 thumbMax(thumbMin.x + thumbPx, thumbMin.y + thumbPx);

                drawList->AddRectFilled(thumbMin, thumbMax,
                                        IM_COL32(244, 246, 250, 255), 3.0f * state.dpiScale);

                if (entry && entry->loaded && !entry->failed && entry->texture.isValid()) {
                    drawList->AddImage((ImTextureID)entry->texture.srv, thumbMin, thumbMax);
                } else {
                    const char* label = "...";
                    if (entry && entry->loaded) {
                        label = entry->extLabel.empty() ? "?" : entry->extLabel.c_str();
                    }
                    const ImVec2 ts = ImGui::CalcTextSize(label);
                    drawList->AddText(ImVec2(thumbMin.x + (thumbPx - ts.x) * 0.5f,
                                             thumbMin.y + (thumbPx - ts.y) * 0.5f),
                                      IM_COL32(140, 150, 165, 255), label);
                }

                const std::string name = fsPathToUtf8(fsPathFromUtf8(path).filename());
                const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
                const float textX = p0.x + std::max(pad * 0.5f, (cellW - nameSize.x) * 0.5f);
                const float textY = thumbMax.y + pad * 0.5f;
                const ImVec4 clipRect(p0.x, p0.y, p0.x + cellW, p0.y + cellH);
                drawList->AddText(nullptr, 0.0f, ImVec2(textX, textY),
                                  IM_COL32(60, 66, 77, 255), name.c_str(), nullptr,
                                  0.0f, &clipRect);

                ImGui::PopID();
            }
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace

void renderRightPanel(AppState& state) {
    // Check if selected file changed
    static int lastSelectedIndex = -2;
    static std::string lastSelectedPath;
    std::string selectedPath;
    if (state.selectedFileIndex >= 0 && state.selectedFileIndex < static_cast<int>(state.fileList.size())) {
        selectedPath = state.fileList[state.selectedFileIndex];
    }

    if (state.selectedFileIndex != lastSelectedIndex || selectedPath != lastSelectedPath) {
        lastSelectedIndex = state.selectedFileIndex;
        lastSelectedPath = selectedPath;
        if (state.selectedFileIndex >= 0 && state.selectedFileIndex < static_cast<int>(state.fileList.size())) {
            updatePreview(state, selectedPath);
        } else {
            state.imageViewer.clearImage();
            state.currentPreviewPath.clear();
            state.currentMeta = ImageMeta();
        }
    }

    // Header: view-mode toggle (网格/预览), always visible.
    {
        auto viewModeBtn = [&](const char* label, int mode) {
            if (state.rightViewMode == mode) PushPrimaryButtonStyle();
            else PushSecondaryButtonStyle();
            const bool clicked = ImGui::Button(label);
            PopButtonStyle();
            if (clicked) state.rightViewMode = mode;
        };
        viewModeBtn("网格", 0);
        ImGui::SameLine();
        viewModeBtn("预览", 1);
        ImGui::SameLine();
        if (state.rightViewMode == 0) {
            ImGui::TextDisabled("共 %d 张 · 双击缩略图进入预览", static_cast<int>(state.fileList.size()));
        } else if (!state.currentPreviewPath.empty()) {
            const std::string name = fsPathToUtf8(fsPathFromUtf8(state.currentPreviewPath).filename());
            ImGui::TextDisabled("%s", name.c_str());
        } else {
            ImGui::TextDisabled("未加载图像");
        }
        ImGui::Separator();
    }

    if (state.rightViewMode == 0) {
        renderThumbnailGrid(state);
        return;
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

        const bool sizeUnchanged = (srcW == targetW && srcH == targetH);
        const bool formatChanged = (state.currentMeta.format != singleSaveFormatStr(state.outputFormat));
        if (sizeUnchanged && !formatChanged) {
            logMsg(state, "尺寸与输出格式均未变化，无需保存");
            return;
        }

        std::string error;
        bool saved = false;
        if (sizeUnchanged) {
            // 仅转换格式，不重采样
            saved = saveAlignedToSource(state, *source, srcW, srcH, &error);
        } else {
            std::vector<uint8_t> resized(static_cast<size_t>(targetW) * targetH * 4);
            stbir_resize_uint8_linear(source->data(), srcW, srcH, srcW * 4,
                                      resized.data(), targetW, targetH, targetW * 4, STBIR_RGBA);
            saved = saveAlignedToSource(state, resized, targetW, targetH, &error);
        }
        if (saved) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "%s：%d x %d", successPrefix, targetW, targetH);
            logMsg(state, msg);
        } else {
            logMsg(state, "保存失败：" + error);
        }
    };

    // Preview canvas (info + toolbar + stage)
    {
        float remainingH = ImGui::GetContentRegionAvail().y;
        const float previewH = std::max(120.0f * state.dpiScale, remainingH);

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(state.previewCanvasBg[0], state.previewCanvasBg[1], state.previewCanvasBg[2], 1.0f));
        ImGui::BeginChild("PreviewArea", ImVec2(-1, previewH), ImGuiChildFlags_Borders);

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

            if (state.currentIsBlp) {
                if (ImGui::Checkbox("显示透明通道", &state.previewUseAlpha)) {
                    refreshPreviewDisplay(state);
                }
                ImGui::SameLine();
            }
            if (ImGui::Button("背景颜色")) {
                ImGui::OpenPopup("BgColorPicker");
            }
            if (ImGui::BeginPopup("BgColorPicker")) {
                ImGui::ColorPicker3("##bgpicker", state.previewCanvasBg);
                if (ImGui::Button("恢复默认")) {
                    state.previewCanvasBg[0] = 0.878f;
                    state.previewCanvasBg[1] = 0.894f;
                    state.previewCanvasBg[2] = 0.922f;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("适应窗口")) {
                state.imageViewer.fitMode = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("原始大小")) {
                state.imageViewer.setZoom(1.0f);
                state.imageViewer.fitMode = false;
            }
            ImGui::SameLine();
            char zoomText[32];
            std::snprintf(zoomText, sizeof(zoomText), "缩放：%d%%",
                          std::max(1, static_cast<int>(state.imageViewer.zoom * 100.0f + 0.5f)));
            ImGui::TextUnformatted(zoomText);

            // Target-size mode -> concrete dimensions (preview save buttons only).
            auto resolveTargetDims = [&](int* tw, int* th) {
                switch (state.targetSizeMode) {
                    case 1: *tw = 64;  *th = 64;  break;
                    case 2: *tw = 128; *th = 128; break;
                    default: *tw = std::max(1, state.resizeWidth);
                             *th = std::max(1, state.resizeHeight); break;
                }
            };
            auto saveWithMethod = [&](ResizeMethod method, const char* okMsg) {
                const std::vector<uint8_t>* src = nullptr;
                int sw = 0, sh = 0;
                if (!getActiveSource(&src, &sw, &sh)) return;
                int tw = 0, th = 0;
                resolveTargetDims(&tw, &th);
                RgbaImage r = transformToTarget(src->data(), sw, sh, tw, th, method);
                if (r.width <= 0) { logMsg(state, "尺寸处理失败"); return; }
                std::string err;
                if (saveAlignedToSource(state, r.pixels, r.width, r.height, &err)) {
                    char msg[128];
                    std::snprintf(msg, sizeof(msg), "%s：%d x %d", okMsg, r.width, r.height);
                    logMsg(state, msg);
                } else {
                    logMsg(state, std::string("保存失败：") + err);
                }
            };

            // New save buttons at the front of the toolbar row (per layout sketch).
            ImGui::SameLine();
            PushPrimaryButtonStyle();
            if (ImGui::Button("拉伸尺寸保存")) saveWithMethod(ResizeMethod::Stretch, "已拉伸尺寸保存");
            PopButtonStyle();
            ImGui::SameLine();
            PushPrimaryButtonStyle();
            if (ImGui::Button("居中透明保存")) saveWithMethod(ResizeMethod::CenterTransparent, "已居中透明保存");
            PopButtonStyle();

            const bool originalIsPot = isPowerOfTwo(state.previewOrigW) && isPowerOfTwo(state.previewOrigH);
            const bool canAlign = !originalIsPot && !state.previewAdjusted;
            ImGui::SameLine();
            if (!canAlign) ImGui::BeginDisabled();
            if (ImGui::Button("拉伸2次幂")) {
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
            ImGui::SameLine();
            if (ImGui::Button("居中2次幂")) {
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

            // Shared target-size selector (默认尺寸 / 64×64 / 128×128) starts the second toolbar row.
            auto sizeModeBtn = [&](const char* label, int mode) {
                if (state.targetSizeMode == mode) PushPrimaryButtonStyle();
                else PushSecondaryButtonStyle();
                bool clicked = ImGui::Button(label);
                PopButtonStyle();
                if (clicked) state.targetSizeMode = mode;
            };
            sizeModeBtn("默认尺寸", 0);
            ImGui::SameLine();
            sizeModeBtn("64×64", 1);
            ImGui::SameLine();
            sizeModeBtn("128×128", 2);
            ImGui::SameLine();

            ImGui::SetNextItemWidth(78.0f * state.dpiScale);
            if (ImGui::InputInt("宽##ToolbarResizeWidth", &state.resizeWidth, 0, 0)) {
                state.resizeWidth = std::clamp(state.resizeWidth, 1, 16384);
                if (state.resizeLockAspect && state.previewOrigW > 0 && state.previewOrigH > 0) {
                    state.resizeAspectSyncing = true;
                    state.resizeHeight = std::max(1,
                        static_cast<int>((static_cast<int64_t>(state.resizeWidth) * state.previewOrigH + state.previewOrigW / 2) / state.previewOrigW));
                    state.resizeAspectSyncing = false;
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(78.0f * state.dpiScale);
            if (ImGui::InputInt("高##ToolbarResizeHeight", &state.resizeHeight, 0, 0)) {
                state.resizeHeight = std::clamp(state.resizeHeight, 1, 16384);
                if (state.resizeLockAspect && state.previewOrigW > 0 && state.previewOrigH > 0) {
                    state.resizeAspectSyncing = true;
                    state.resizeWidth = std::max(1,
                        static_cast<int>((static_cast<int64_t>(state.resizeHeight) * state.previewOrigW + state.previewOrigH / 2) / state.previewOrigH));
                    state.resizeAspectSyncing = false;
                }
            }
            ImGui::SameLine();
            ImGui::Checkbox("锁比例", &state.resizeLockAspect);
            ImGui::SameLine();
            PushPrimaryButtonStyle();
            if (ImGui::Button("保存尺寸")) {
                resizeAndSave(std::max(1, state.resizeWidth), std::max(1, state.resizeHeight), "已调整图像尺寸并保存");
            }
            PopButtonStyle();
            ImGui::SameLine();
            // 跨格式撤销：当前文件正是上次格式转换的产物时，可整体还原
            const bool convertedRestore = !state.convertedFromBytes.empty() &&
                                          state.currentPreviewPath == state.convertedToPath;
            const bool canRestore = (state.previewAdjusted && state.hasOriginalBackup) || convertedRestore;
            if (!canRestore) ImGui::BeginDisabled();
            PushDangerButtonStyle();
            if (ImGui::Button("恢复原图")) {
                std::string error;
                if (convertedRestore) {
                    if (writeFileBytes(state.convertedFromPath, state.convertedFromBytes, &error)) {
                        namespace fs = std::filesystem;
                        std::error_code ec;
                        fs::remove(fsPathFromUtf8(state.convertedToPath), ec);
                        state.thumbCache.invalidate(state.convertedToPath);
                        state.thumbCache.invalidate(state.convertedFromPath);

                        // 列表条目换回原路径
                        auto it = std::find(state.fileList.begin(), state.fileList.end(),
                                            state.convertedToPath);
                        const bool origListed = state.fileSet.count(state.convertedFromPath) != 0;
                        if (it != state.fileList.end()) {
                            if (origListed) {
                                state.fileList.erase(it);
                            } else {
                                *it = state.convertedFromPath;
                            }
                            state.fileSet.erase(state.convertedToPath);
                            state.relativePathMap.erase(state.convertedToPath);
                        }
                        if (!origListed) {
                            state.fileSet.insert(state.convertedFromPath);
                        }
                        auto nit = std::find(state.fileList.begin(), state.fileList.end(),
                                             state.convertedFromPath);
                        if (nit != state.fileList.end()) {
                            state.selectedFileIndex = static_cast<int>(nit - state.fileList.begin());
                        }
                        state.currentPreviewPath = state.convertedFromPath;
                        state.convertedFromPath.clear();
                        state.convertedFromBytes.clear();
                        state.convertedToPath.clear();
                        updatePreview(state, state.currentPreviewPath);
                        logMsg(state, "已恢复原始文件");
                    } else {
                        logMsg(state, "恢复失败：" + error);
                    }
                } else if (writeFileBytes(state.currentPreviewPath, state.originalFileBytes, &error)) {
                    state.thumbCache.invalidate(state.currentPreviewPath);
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
        } else {
            ImGui::TextUnformatted("未加载图像");
        }

        ImGui::Separator();

        const ImVec2 stagePos = ImGui::GetCursorPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        state.imageViewer.render(avail.x, avail.y);

        // Handle resize on fit
        if (state.imageViewer.fitMode && state.imageViewer.hasImage) {
            state.imageViewer.fitToView(avail.x, avail.y);
        }

        state.zoomPercent = std::max(1, static_cast<int>(state.imageViewer.zoom * 100.0f + 0.5f));

        if (hasImage && state.currentIsBlp && !state.mipEntries.empty()) {
            const float pad = 8.0f * state.dpiScale;
            const float panelW = std::min(220.0f * state.dpiScale, std::max(150.0f * state.dpiScale, avail.x * 0.28f));
            const float lineH = ImGui::GetTextLineHeightWithSpacing();
            const int maxLinesByHeight = std::max(1, static_cast<int>((avail.y - 56.0f * state.dpiScale) / lineH));
            const int maxLines = std::min(6, maxLinesByHeight);
            const int shownCount = std::min(static_cast<int>(state.mipEntries.size()), maxLines);
            const int restCount = static_cast<int>(state.mipEntries.size()) - shownCount;
            float panelH = lineH + shownCount * lineH + (restCount > 0 ? lineH : 0.0f) + 8.0f * state.dpiScale;
            panelH = std::min(panelH, std::max(80.0f * state.dpiScale, avail.y - 2.0f * pad));

            const float panelX = stagePos.x + avail.x - panelW - pad;
            const float panelY = std::max(stagePos.y + pad, stagePos.y + avail.y - panelH - pad);

            ImGui::SetCursorPos(ImVec2(panelX, panelY));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.22f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.961f, 0.973f, 1.000f, 0.95f));
            ImGui::BeginChild(
                "CanvasMipOverlay",
                ImVec2(panelW, panelH),
                ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            ImGui::TextUnformatted("BLP 层级");

            const int baseW = std::max(1, state.currentMeta.width);
            const int baseH = std::max(1, state.currentMeta.height);
            for (int i = 0; i < shownCount; ++i) {
                const BlpMipEntry& entry = state.mipEntries[i];
                const int mipW = std::max(1, baseW >> entry.index);
                const int mipH = std::max(1, baseH >> entry.index);
                std::string sizeStr = formatFileSize(entry.size);
                ImGui::Text("第%d层 %d x %d | %s", entry.index + 1, mipW, mipH, sizeStr.c_str());
            }
            if (restCount > 0) {
                ImGui::Text("其余 %d 层...", restCount);
            }

            ImGui::EndChild();
            ImGui::PopStyleColor(2);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
}
