#include "ui_left_panel.h"
#include "ui_main.h"
#include "file_dialogs.h"
#include "style.h"
#include "utils.h"
#include "image_ops.h"
#include "win_integration.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), sz, nullptr, nullptr);
    return result;
}

std::string toLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

void logMessage(AppState& state, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_s(&tm_buf, &t);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm_buf);
    char buf[512];
    std::snprintf(buf, sizeof(buf), "[%s] %s", timeBuf, msg.c_str());
    state.logMessages.push_back(buf);
}

void addFiles(AppState& state, const std::vector<std::string>& paths);

void addFolderFiles(AppState& state, const std::string& folder, bool recursive) {
    if (folder.empty()) return;

    namespace fs = std::filesystem;
    const fs::path folderPath = fsPathFromUtf8(folder);
    auto exts = supportedExtensions();

    std::vector<std::string> paths;
    std::unordered_map<std::string, std::string> relMap;

    auto processEntry = [&](const fs::directory_entry& entry) {
        if (!entry.is_regular_file()) return;
        std::string ext = entry.path().extension().string();
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
        std::string normExt = normalizeFormat(ext);
        bool supported = false;
        for (const auto& e : exts) {
            if (normExt == e) { supported = true; break; }
        }
        if (!supported) return;
        std::string fullPath = fsPathToUtf8(entry.path());
        paths.push_back(fullPath);
        std::string relPath = fsPathToUtf8(fs::relative(entry.path(), folderPath));
        if (!relPath.empty()) {
            relMap[fullPath] = relPath;
        }
    };

    try {
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(folderPath))
                processEntry(entry);
        } else {
            for (const auto& entry : fs::directory_iterator(folderPath))
                processEntry(entry);
        }
    } catch (...) {}

    // Merge relative maps
    for (const auto& [k, v] : relMap) {
        state.relativePathMap[k] = v;
    }

    const int beforeCount = static_cast<int>(state.fileList.size());
    addFiles(state, paths);
    // Folder add is a batch intent -> 网格, even for a single-image folder.
    if (static_cast<int>(state.fileList.size()) > beforeCount) {
        state.rightViewMode = 0;
    }
}

void addFiles(AppState& state, const std::vector<std::string>& paths) {
    int startCount = static_cast<int>(state.fileList.size());
    bool addedViaFolder = false;

    for (const auto& path : paths) {
        namespace fs = std::filesystem;
        try {
            fs::path fsPath = fsPathFromUtf8(path);
            if (!fs::exists(fsPath)) continue;
            if (fs::is_directory(fsPath)) {
                addedViaFolder = true;
                addFolderFiles(state, path, state.recursive);
                continue;
            }
            if (!isSupportedFile(path)) continue;

            std::string fullPath = fsPathToUtf8(fs::absolute(fsPath));
            if (state.fileSet.count(fullPath)) continue;

            state.fileList.push_back(fullPath);
            state.fileSet.insert(fullPath);
        } catch (...) {}
    }

    const int added = static_cast<int>(state.fileList.size()) - startCount;
    if (added > 0) {
        state.selectedFileIndex = static_cast<int>(state.fileList.size()) - 1;
        // Single file opened -> 预览; batch add (folder or multiple) -> 网格.
        state.rightViewMode = (addedViaFolder || added > 1) ? 0 : 1;
    } else if (!state.fileList.empty() && state.selectedFileIndex < 0) {
        state.selectedFileIndex = 0;
    }
}

std::string normalizedFormatStr(int formatIndex) {
    const char* formats[] = {"blp", "png", "jpg", "bmp", "tga"};
    if (formatIndex >= 0 && formatIndex < 5) return formats[formatIndex];
    return "blp";
}

std::string buildOutputPath(const AppState& state, const std::string& inputPath,
                            const std::string& format, bool overwrite) {
    namespace fs = std::filesystem;
    std::string outputDir = state.outputDirBuf;
    fs::path outputDirPath = fsPathFromUtf8(outputDir);
    auto it = state.relativePathMap.find(inputPath);
    std::string baseName;
    std::string relativeDir;
    if (it != state.relativePathMap.end()) {
        fs::path relPath = fsPathFromUtf8(it->second);
        baseName = fsPathToUtf8(relPath.stem());
        relativeDir = fsPathToUtf8(relPath.parent_path());
        if (relativeDir == ".") relativeDir.clear();
    } else {
        baseName = fsPathToUtf8(fsPathFromUtf8(inputPath).stem());
    }
    std::string ext = normalizeFormat(format);
    fs::path targetDirPath = relativeDir.empty()
                                 ? outputDirPath
                                 : (outputDirPath / fsPathFromUtf8(relativeDir));

    fs::path candidatePath = targetDirPath / fsPathFromUtf8(baseName + "." + ext);
    std::string candidate = fsPathToUtf8(candidatePath);
    if (overwrite || !fs::exists(candidatePath)) return candidate;

    int index = 1;
    while (fs::exists(candidatePath)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "_%d.", index);
        candidatePath = targetDirPath / fsPathFromUtf8(baseName + buf + ext);
        candidate = fsPathToUtf8(candidatePath);
        ++index;
    }
    return candidate;
}

bool ensureOutputDir(AppState& state, const std::vector<std::string>& inputPaths) {
    std::string outputDir = state.outputDirBuf;
    if (!outputDir.empty()) return true;

    if (inputPaths.size() == 1) {
        outputDir = fsPathToUtf8(fsPathFromUtf8(inputPaths[0]).parent_path());
        std::strncpy(state.outputDirBuf, outputDir.c_str(), sizeof(state.outputDirBuf) - 1);
        logMessage(state, "未设置输出目录，已使用源文件目录");
        return !outputDir.empty();
    }

    logMessage(state, "请先设置输出目录");
    return false;
}

// Resolve the batch 输出尺寸 selection; returns false for 原始大小 (no resize).
bool resolveBatchTarget(const AppState& state, int* tw, int* th) {
    switch (state.batchSizeMode) {
        case 1: *tw = 64;  *th = 64;  return true;
        case 2: *tw = 128; *th = 128; return true;
        case 3: *tw = std::clamp(state.batchWidth, 1, 16384);
                *th = std::clamp(state.batchHeight, 1, 16384); return true;
        default: return false;
    }
}

void applyBatchOutputSize(const AppState& state, RgbaImage& image) {
    int tw = 0, th = 0;
    if (!resolveBatchTarget(state, &tw, &th)) return;
    if (tw == image.width && th == image.height) return;
    const ResizeMethod method = (state.batchResizeMethod == 0)
        ? ResizeMethod::Stretch : ResizeMethod::CenterTransparent;
    RgbaImage resized = transformToTarget(image.pixels.data(), image.width, image.height,
                                          tw, th, method);
    if (resized.width > 0) image = std::move(resized);
}

void runConvertForPathsInternal(AppState& state, const std::vector<std::string>& inputPaths) {
    if (state.converting.load()) {
        logMessage(state, "已有转换任务正在执行");
        return;
    }
    if (inputPaths.empty()) {
        logMessage(state, "没有可转换的文件");
        return;
    }
    if (!ensureOutputDir(state, inputPaths)) return;

    const std::string format = normalizedFormatStr(state.outputFormat);
    const bool formatIsBlp = (format == "blp");

    const int total = static_cast<int>(inputPaths.size());
    int successCount = 0;
    int failedCount = 0;

    state.lastConvertResults.clear();
    state.lastConvertResults.reserve(inputPaths.size());
    state.lastConvertTotal = total;
    state.lastConvertSuccess = 0;
    state.lastConvertFailed = 0;

    state.converting = true;
    state.convertProgress = 0.0f;

    for (int i = 0; i < total; ++i) {
        const std::string& inputPath = inputPaths[i];
        ConvertItemResult item;
        item.inputPath = inputPath;

        RgbaImage image;
        ImageMeta meta;
        std::string error;
        if (!loadImageFile(inputPath, &image, &meta, &error, &state.blpApi)) {
            item.success = false;
            item.error = error;
            char msg[512];
            std::snprintf(msg, sizeof(msg), "读取失败：%s（%s）", inputPath.c_str(), error.c_str());
            logMessage(state, msg);
            ++failedCount;
            state.lastConvertResults.push_back(std::move(item));
            state.convertProgress = static_cast<float>(i + 1) / total;
            continue;
        }

        applyBatchOutputSize(state, image);

        std::string outPath = buildOutputPath(state, inputPath, format, state.overwrite);
        item.outputPath = outPath;
        const int mipCount = formatIsBlp ? autoMipCount(image.width, image.height) : 1;
        if (!writeImageFile(outPath, format, image, state.quality, mipCount, &error, &state.blpApi)) {
            item.success = false;
            item.error = error;
            char msg[512];
            std::snprintf(msg, sizeof(msg), "写入失败：%s（%s）", outPath.c_str(), error.c_str());
            logMessage(state, msg);
            ++failedCount;
            state.lastConvertResults.push_back(std::move(item));
            state.convertProgress = static_cast<float>(i + 1) / total;
            continue;
        }

        item.success = true;
        if (state.fileSet.count(outPath)) state.thumbCache.invalidate(outPath);
        ++successCount;
        state.lastConvertResults.push_back(std::move(item));
        state.convertProgress = static_cast<float>(i + 1) / total;
    }

    state.lastConvertSuccess = successCount;
    state.lastConvertFailed = failedCount;
    char msg[160];
    std::snprintf(msg, sizeof(msg), "已转换 %d / %d 个文件（失败 %d）",
                  successCount, total, failedCount);
    logMessage(state, msg);
    state.converting = false;
}

void doConvertAll(AppState& state) {
    if (state.fileList.empty()) {
        std::string inputDir = state.inputDirBuf;
        if (!inputDir.empty()) {
            addFolderFiles(state, inputDir, state.recursive);
        }
    }

    runConvertForPathsInternal(state, state.fileList);
}

// Shared "输出格式" selector. Bound to state.outputFormat, which drives both
// batch conversion and single-image save (保存尺寸/对齐), so it is shown in both
// the 资源列表 and 批量转换 tabs and stays in sync.
void drawOutputFormatSelector(AppState& state) {
    ImGui::TextUnformatted("输出格式");
    auto drawFormatButton = [&](const char* label, int formatId, float width) {
        if (state.outputFormat == formatId) {
            PushPrimaryButtonStyle();
        } else {
            PushSecondaryButtonStyle();
        }
        const bool clicked = ImGui::Button(label, ImVec2(width, 0));
        PopButtonStyle();
        if (clicked) {
            state.outputFormat = formatId;
        }
    };
    const float formatSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float formatAvail = ImGui::GetContentRegionAvail().x;
    const float topRowW = std::max(1.0f, (formatAvail - formatSpacing * 2.0f) / 3.0f);
    const float bottomRowW = std::max(1.0f, (formatAvail - formatSpacing) / 2.0f);
    drawFormatButton("BLP", 0, topRowW);
    ImGui::SameLine();
    drawFormatButton("PNG", 1, topRowW);
    ImGui::SameLine();
    drawFormatButton("JPG", 2, topRowW);
    drawFormatButton("BMP", 3, bottomRowW);
    ImGui::SameLine();
    drawFormatButton("TGA", 4, bottomRowW);
}

// Shared "输出尺寸" block for 批量转换 / 图层合成: target size + fit method.
void drawOutputSizeSelector(AppState& state) {
    ImGui::TextUnformatted("输出尺寸");
    auto sizeModeButton = [&](const char* label, int mode, float width) {
        if (state.batchSizeMode == mode) PushPrimaryButtonStyle();
        else PushSecondaryButtonStyle();
        const bool clicked = ImGui::Button(label, ImVec2(width, 0));
        PopButtonStyle();
        if (clicked) state.batchSizeMode = mode;
    };
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float halfW = std::max(1.0f, (ImGui::GetContentRegionAvail().x - spacing) / 2.0f);
    sizeModeButton("原始大小", 0, halfW);
    ImGui::SameLine();
    sizeModeButton("64×64", 1, halfW);
    sizeModeButton("128×128", 2, halfW);
    ImGui::SameLine();
    sizeModeButton("自定义", 3, halfW);

    if (state.batchSizeMode == 3) {
        ImGui::SetNextItemWidth(78.0f * state.dpiScale);
        if (ImGui::InputInt("宽##BatchW", &state.batchWidth, 0, 0)) {
            state.batchWidth = std::clamp(state.batchWidth, 1, 16384);
            if (state.batchLockAspect && state.batchAspect > 0.0f) {
                state.batchHeight = std::clamp(
                    static_cast<int>(state.batchWidth / state.batchAspect + 0.5f), 1, 16384);
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(78.0f * state.dpiScale);
        if (ImGui::InputInt("高##BatchH", &state.batchHeight, 0, 0)) {
            state.batchHeight = std::clamp(state.batchHeight, 1, 16384);
            if (state.batchLockAspect && state.batchAspect > 0.0f) {
                state.batchWidth = std::clamp(
                    static_cast<int>(state.batchHeight * state.batchAspect + 0.5f), 1, 16384);
            }
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("锁比例##Batch", &state.batchLockAspect) && state.batchLockAspect) {
            state.batchAspect = static_cast<float>(std::max(1, state.batchWidth)) /
                                static_cast<float>(std::max(1, state.batchHeight));
        }
    }

    if (state.batchSizeMode != 0) {
        ImGui::TextUnformatted("处理方式");
        auto methodButton = [&](const char* label, int method, float width) {
            if (state.batchResizeMethod == method) PushPrimaryButtonStyle();
            else PushSecondaryButtonStyle();
            const bool clicked = ImGui::Button(label, ImVec2(width, 0));
            PopButtonStyle();
            if (clicked) state.batchResizeMethod = method;
        };
        methodButton("拉伸##BatchMethod", 0, halfW);
        ImGui::SameLine();
        methodButton("居中透明##BatchMethod", 1, halfW);
    }
}

} // namespace

void renderLeftPanel(AppState& state) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 1));
    ImGui::TextUnformatted("资源中心");
    ImGui::Separator();

    if (ImGui::BeginTabBar("LeftWorkspaceTabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        if (ImGui::BeginTabItem("资源列表")) {
            static char searchBuf[128] = {};

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.357f, 0.392f, 0.447f, 1.0f));
            ImGui::TextWrapped("拖拽、按钮添加或从菜单栏导入。列表用于预览与批量转换。");
            ImGui::PopStyleColor();

            ImGui::PushItemWidth(-1);
            ImGui::InputTextWithHint("##FileSearch", "搜索文件名...", searchBuf, sizeof(searchBuf));
            ImGui::PopItemWidth();

            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float halfW = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;

            if (ImGui::Button("添加文件...", ImVec2(halfW, 0))) {
                auto files = openFileDialog(state.hwnd, L"*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.blp", true);
                std::vector<std::string> paths;
                for (const auto& f : files) paths.push_back(wideToUtf8(f));
                addFiles(state, paths);
            }
            ImGui::SameLine();
            if (ImGui::Button("添加文件夹...", ImVec2(-1, 0))) {
                auto folder = openFolderDialog(state.hwnd);
                if (!folder.empty()) {
                    std::string folderUtf8 = wideToUtf8(folder);
                    std::strncpy(state.inputDirBuf, folderUtf8.c_str(), sizeof(state.inputDirBuf) - 1);
                    addFolderFiles(state, folderUtf8, state.recursive);
                }
            }
            if (ImGui::Button("移除选中", ImVec2(halfW, 0))) {
                if (state.selectedFileIndex >= 0 && state.selectedFileIndex < static_cast<int>(state.fileList.size())) {
                    std::string path = state.fileList[state.selectedFileIndex];
                    const bool removedCurrentPreview = (path == state.currentPreviewPath);
                    state.thumbCache.invalidate(path);
                    state.fileSet.erase(path);
                    state.relativePathMap.erase(path);
                    state.fileList.erase(state.fileList.begin() + state.selectedFileIndex);
                    if (state.fileList.empty()) {
                        state.selectedFileIndex = -1;
                        state.imageViewer.clearImage();
                        state.currentPreviewPath.clear();
                        state.currentBlpBytes.clear();
                        state.previewOriginalRGBA.clear();
                        state.previewAdjustedRGBA.clear();
                        state.currentMeta = ImageMeta();
                    } else if (state.selectedFileIndex >= static_cast<int>(state.fileList.size())) {
                        state.selectedFileIndex = static_cast<int>(state.fileList.size()) - 1;
                        if (removedCurrentPreview) {
                            state.currentPreviewPath.clear();
                        }
                    } else if (removedCurrentPreview) {
                        state.currentPreviewPath.clear();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("清空列表", ImVec2(-1, 0))) {
                state.thumbCache.clear();
                state.fileList.clear();
                state.fileSet.clear();
                state.relativePathMap.clear();
                state.selectedFileIndex = -1;
                state.imageViewer.clearImage();
                state.currentPreviewPath.clear();
                state.currentBlpBytes.clear();
                state.previewOriginalRGBA.clear();
                state.previewAdjustedRGBA.clear();
                state.currentMeta = ImageMeta();
            }

            ImGui::Separator();
            drawOutputFormatSelector(state);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.357f, 0.392f, 0.447f, 1.0f));
            ImGui::TextWrapped("用于预览图“保存尺寸/对齐”的保存格式，与批量转换共用此设置。");
            ImGui::PopStyleColor();
            ImGui::Separator();

            const std::string keyword = toLowerAscii(searchBuf);
            float listH = ImGui::GetContentRegionAvail().y - 28.0f * state.dpiScale;
            if (listH < 120.0f * state.dpiScale) listH = 120.0f * state.dpiScale;
            ImGui::BeginChild("FileList", ImVec2(-1, listH), ImGuiChildFlags_Borders);
            for (int i = 0; i < static_cast<int>(state.fileList.size()); ++i) {
                const std::string displayName = fsPathToUtf8(fsPathFromUtf8(state.fileList[i]).filename());
                if (!keyword.empty()) {
                    const std::string nameLower = toLowerAscii(displayName);
                    const std::string pathLower = toLowerAscii(state.fileList[i]);
                    if (nameLower.find(keyword) == std::string::npos &&
                        pathLower.find(keyword) == std::string::npos) {
                        continue;
                    }
                }

                bool isSelected = (i == state.selectedFileIndex);
                if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                    state.selectedFileIndex = i;
                    state.rightViewMode = 1;  // 点击列表项即打开单图预览
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", state.fileList[i].c_str());
                }
            }
            ImGui::EndChild();

            char brief[96];
            std::snprintf(brief, sizeof(brief), "总文件数：%zu | 当前选中：%d",
                          state.fileList.size(),
                          state.selectedFileIndex >= 0 ? state.selectedFileIndex + 1 : 0);
            ImGui::TextUnformatted(brief);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("批量转换")) {
            ImGui::TextUnformatted("输入来源");
            ImGui::PushItemWidth(-84);
            ImGui::InputText("##InputDir", state.inputDirBuf, sizeof(state.inputDirBuf));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("浏览##InputBrowse")) {
                auto folder = openFolderDialog(state.hwnd);
                if (!folder.empty()) {
                    std::strncpy(state.inputDirBuf, wideToUtf8(folder).c_str(), sizeof(state.inputDirBuf) - 1);
                }
            }
            ImGui::Checkbox("包含子目录", &state.recursive);
            ImGui::SameLine();
            if (ImGui::Button("扫描并添加")) {
                std::string dir = state.inputDirBuf;
                if (!dir.empty()) {
                    namespace fs = std::filesystem;
                    fs::path dirPath = fsPathFromUtf8(dir);
                    if (fs::exists(dirPath) && fs::is_directory(dirPath)) {
                        addFolderFiles(state, dir, state.recursive);
                    } else {
                        logMessage(state, "输入目录不存在");
                    }
                } else {
                    logMessage(state, "请先设置输入目录");
                }
            }

            ImGui::Separator();

            ImGui::TextUnformatted("输出设置");
            ImGui::PushItemWidth(-84);
            ImGui::InputText("##OutputDir", state.outputDirBuf, sizeof(state.outputDirBuf));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("浏览##OutputBrowse")) {
                auto folder = openFolderDialog(state.hwnd);
                if (!folder.empty()) {
                    std::strncpy(state.outputDirBuf, wideToUtf8(folder).c_str(), sizeof(state.outputDirBuf) - 1);
                }
            }

            drawOutputFormatSelector(state);

            if (state.outputFormat == 0) {
                ImGui::SliderInt("BLP 质量", &state.quality, 0, 100);
            } else {
                ImGui::TextDisabled("非 BLP 输出质量：%d（可在“编辑”菜单调整）", state.quality);
            }

            ImGui::Separator();
            drawOutputSizeSelector(state);
            ImGui::Separator();

            ImGui::Checkbox("覆盖已存在文件", &state.overwrite);

            ImGui::Spacing();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
            if (state.converting.load()) {
                ImGui::BeginDisabled();
                ImGui::Button("转换中...", ImVec2(-1, 0));
                ImGui::EndDisabled();
            } else {
                PushPrimaryButtonStyle();
                if (ImGui::Button("开始转换", ImVec2(-1, 0))) {
                    ImGui::OpenPopup("确认开始转换");
                }
                PopButtonStyle();
            }
            ImGui::PopStyleVar();

            if (ImGui::BeginPopupModal("确认开始转换", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                const bool hasOutputDir = state.outputDirBuf[0] != '\0';
                const bool hasFiles = !state.fileList.empty();
                const bool hasInputDir = state.inputDirBuf[0] != '\0';
                const char* formatNames[] = {"BLP", "PNG", "JPG", "BMP", "TGA"};
                const char* formatName = formatNames[std::clamp(state.outputFormat, 0, 4)];

                if (hasFiles) {
                    ImGui::Text("将转换 %d 个文件，输出格式：%s",
                                static_cast<int>(state.fileList.size()), formatName);
                } else if (hasInputDir) {
                    ImGui::Text("资源列表为空，将先扫描输入目录再转换，输出格式：%s", formatName);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, UiColorError());
                    ImGui::TextUnformatted("没有可转换的文件，请先添加文件或设置输入目录。");
                    ImGui::PopStyleColor();
                }

                char sizeDesc[96];
                const char* methodName = (state.batchResizeMethod == 0) ? "拉伸" : "居中透明";
                switch (state.batchSizeMode) {
                    case 1: std::snprintf(sizeDesc, sizeof(sizeDesc), "输出尺寸：64×64（%s）", methodName); break;
                    case 2: std::snprintf(sizeDesc, sizeof(sizeDesc), "输出尺寸：128×128（%s）", methodName); break;
                    case 3: std::snprintf(sizeDesc, sizeof(sizeDesc), "输出尺寸：%d×%d（%s）",
                                          state.batchWidth, state.batchHeight, methodName); break;
                    default: std::snprintf(sizeDesc, sizeof(sizeDesc), "输出尺寸：原始大小"); break;
                }
                ImGui::TextUnformatted(sizeDesc);

                if (hasOutputDir) {
                    ImGui::Text("输出目录：");
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", state.outputDirBuf);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, UiColorWarning());
                    ImGui::TextWrapped("尚未设置输出目录！转换结果将直接保存到各源文件所在目录。");
                    ImGui::PopStyleColor();
                    if (ImGui::Button("选择输出目录...")) {
                        auto folder = openFolderDialog(state.hwnd);
                        if (!folder.empty()) {
                            std::strncpy(state.outputDirBuf, wideToUtf8(folder).c_str(),
                                         sizeof(state.outputDirBuf) - 1);
                        }
                    }
                }

                ImGui::Separator();
                const float btnW = 120.0f * state.dpiScale;
                if (hasFiles || hasInputDir) {
                    PushPrimaryButtonStyle();
                    if (ImGui::Button("确认转换", ImVec2(btnW, 0))) {
                        ImGui::CloseCurrentPopup();
                        runConvertAllFromUi(state);
                    }
                    PopButtonStyle();
                    ImGui::SameLine();
                }
                if (ImGui::Button("取消", ImVec2(btnW, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::ProgressBar(state.convertProgress.load(), ImVec2(-1, 0));

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("图层合成")) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.357f, 0.392f, 0.447f, 1.0f));
            ImGui::TextWrapped("对“资源列表”中的每张图执行图层叠加或加边框，批量输出。");
            ImGui::PopStyleColor();

            ImGui::RadioButton("图层叠加", &state.composeOpMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("边框", &state.composeOpMode, 1);
            ImGui::Separator();

            if (state.composeOpMode == 0) {
                if (ImGui::Button("选择叠加图...")) {
                    auto files = openFileDialog(state.hwnd, L"*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.blp", false);
                    if (!files.empty()) {
                        state.overlayImagePath = wideToUtf8(files.front());
                    }
                }
                ImGui::SameLine();
                if (state.overlayImagePath.empty()) {
                    ImGui::TextDisabled("未选择");
                } else {
                    std::string name = fsPathToUtf8(fsPathFromUtf8(state.overlayImagePath).filename());
                    ImGui::TextUnformatted(name.c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", state.overlayImagePath.c_str());
                }

                ImGui::TextUnformatted("锚点");
                const char* anchorLabels[9] = {"左上", "上", "右上",
                                               "左", "中", "右",
                                               "左下", "下", "右下"};
                const float anchorBtnW = std::max(1.0f,
                    (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f);
                for (int a = 0; a < 9; ++a) {
                    if (state.overlayAnchor == a) PushPrimaryButtonStyle();
                    else PushSecondaryButtonStyle();
                    char id[32];
                    std::snprintf(id, sizeof(id), "%s##anchor%d", anchorLabels[a], a);
                    if (ImGui::Button(id, ImVec2(anchorBtnW, 0))) state.overlayAnchor = a;
                    PopButtonStyle();
                    if (a % 3 != 2) ImGui::SameLine();
                }

                ImGui::SliderInt("边距(px)", &state.overlayMarginPx, 0, 256);
                ImGui::SliderInt("缩放(%)", &state.overlayScalePct, 1, 400);
                ImGui::SliderFloat("不透明度", &state.overlayOpacity, 0.0f, 1.0f, "%.2f");
            } else {
                ImGui::SliderInt("边框厚度(px)", &state.borderThicknessPx, 0, 256);
                ImGui::ColorEdit4("边框颜色", state.borderColor);
                ImGui::Checkbox("扩展画布(否则内描边)", &state.borderExpandCanvas);
            }

            ImGui::Separator();
            ImGui::TextUnformatted("输出设置");
            ImGui::PushItemWidth(-84);
            ImGui::InputText("##ComposeOutputDir", state.outputDirBuf, sizeof(state.outputDirBuf));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("浏览##ComposeOutputBrowse")) {
                auto folder = openFolderDialog(state.hwnd);
                if (!folder.empty()) {
                    std::strncpy(state.outputDirBuf, wideToUtf8(folder).c_str(), sizeof(state.outputDirBuf) - 1);
                }
            }
            drawOutputFormatSelector(state);
            if (state.outputFormat == 0) {
                ImGui::SliderInt("BLP 质量##compose", &state.quality, 0, 100);
            }
            drawOutputSizeSelector(state);
            ImGui::Checkbox("覆盖已存在文件##compose", &state.overwrite);

            ImGui::Spacing();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
            if (state.converting.load()) {
                ImGui::BeginDisabled();
                ImGui::Button("处理中...", ImVec2(-1, 0));
                ImGui::EndDisabled();
            } else {
                PushPrimaryButtonStyle();
                if (ImGui::Button("开始处理", ImVec2(-1, 0))) {
                    runComposeBatchFromUi(state);
                }
                PopButtonStyle();
            }
            ImGui::PopStyleVar();
            ImGui::ProgressBar(state.convertProgress.load(), ImVec2(-1, 0));

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::PopStyleColor(); // ChildBg
}

void runConvertAllFromUi(AppState& state) {
    doConvertAll(state);
}

void runComposeBatchFromUi(AppState& state) {
    if (state.converting.load()) {
        logMessage(state, "已有任务正在执行");
        return;
    }
    const std::vector<std::string>& inputPaths = state.fileList;
    if (inputPaths.empty()) {
        logMessage(state, "资源列表为空，无可处理文件");
        return;
    }
    if (!ensureOutputDir(state, inputPaths)) return;

    // Overlay mode: load the overlay image once up front.
    RgbaImage overlayImg;
    if (state.composeOpMode == 0) {
        if (state.overlayImagePath.empty()) {
            logMessage(state, "请先选择叠加图");
            return;
        }
        ImageMeta om;
        std::string oerr;
        if (!loadImageFile(state.overlayImagePath, &overlayImg, &om, &oerr, &state.blpApi)) {
            logMessage(state, std::string("叠加图读取失败：") + oerr);
            return;
        }
    }

    const std::string format = normalizedFormatStr(state.outputFormat);
    const bool formatIsBlp = (format == "blp");
    const int total = static_cast<int>(inputPaths.size());
    int successCount = 0;
    int failedCount = 0;

    state.lastConvertResults.clear();
    state.lastConvertResults.reserve(inputPaths.size());
    state.lastConvertTotal = total;
    state.lastConvertSuccess = 0;
    state.lastConvertFailed = 0;
    state.converting = true;
    state.convertProgress = 0.0f;

    for (int i = 0; i < total; ++i) {
        const std::string& inputPath = inputPaths[i];
        ConvertItemResult item;
        item.inputPath = inputPath;

        RgbaImage image;
        ImageMeta meta;
        std::string error;
        if (!loadImageFile(inputPath, &image, &meta, &error, &state.blpApi)) {
            item.success = false;
            item.error = error;
            char msg[512];
            std::snprintf(msg, sizeof(msg), "读取失败：%s（%s）", inputPath.c_str(), error.c_str());
            logMessage(state, msg);
            ++failedCount;
            state.lastConvertResults.push_back(std::move(item));
            state.convertProgress = static_cast<float>(i + 1) / total;
            continue;
        }

        RgbaImage out;
        if (state.composeOpMode == 0) {
            out = compositeOverlay(image, overlayImg, static_cast<Anchor9>(state.overlayAnchor),
                                   state.overlayMarginPx, state.overlayScalePct, state.overlayOpacity);
        } else {
            auto to8 = [](float v) { return static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f)); };
            out = addBorder(image, state.borderThicknessPx,
                            to8(state.borderColor[0]), to8(state.borderColor[1]),
                            to8(state.borderColor[2]), to8(state.borderColor[3]),
                            state.borderExpandCanvas);
        }
        if (out.width <= 0) out = image;  // safety fallback

        applyBatchOutputSize(state, out);

        std::string outPath = buildOutputPath(state, inputPath, format, state.overwrite);
        item.outputPath = outPath;
        const int mipCount = formatIsBlp ? autoMipCount(out.width, out.height) : 1;
        if (!writeImageFile(outPath, format, out, state.quality, mipCount, &error, &state.blpApi)) {
            item.success = false;
            item.error = error;
            char msg[512];
            std::snprintf(msg, sizeof(msg), "写入失败：%s（%s）", outPath.c_str(), error.c_str());
            logMessage(state, msg);
            ++failedCount;
            state.lastConvertResults.push_back(std::move(item));
            state.convertProgress = static_cast<float>(i + 1) / total;
            continue;
        }

        item.success = true;
        if (state.fileSet.count(outPath)) state.thumbCache.invalidate(outPath);
        ++successCount;
        state.lastConvertResults.push_back(std::move(item));
        state.convertProgress = static_cast<float>(i + 1) / total;
    }

    state.lastConvertSuccess = successCount;
    state.lastConvertFailed = failedCount;
    char msg[160];
    std::snprintf(msg, sizeof(msg), "已处理 %d / %d 个文件（失败 %d）",
                  successCount, total, failedCount);
    logMessage(state, msg);
    state.converting = false;
}

void runConvertForPaths(AppState& state, const std::vector<std::string>& inputPaths) {
    runConvertForPathsInternal(state, inputPaths);
}
