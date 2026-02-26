#include "ui_left_panel.h"
#include "ui_main.h"
#include "file_dialogs.h"
#include "style.h"
#include "utils.h"
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

    addFiles(state, paths);
}

void addFiles(AppState& state, const std::vector<std::string>& paths) {
    int startCount = static_cast<int>(state.fileList.size());

    for (const auto& path : paths) {
        namespace fs = std::filesystem;
        try {
            fs::path fsPath = fsPathFromUtf8(path);
            if (!fs::exists(fsPath)) continue;
            if (fs::is_directory(fsPath)) {
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

    if (static_cast<int>(state.fileList.size()) > startCount) {
        state.selectedFileIndex = static_cast<int>(state.fileList.size()) - 1;
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

    state.taskDrawerVisible = true;
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

            if (state.outputFormat == 0) {
                ImGui::SliderInt("BLP 质量", &state.quality, 0, 100);
            } else {
                ImGui::TextDisabled("非 BLP 输出质量：%d（可在“编辑”菜单调整）", state.quality);
            }

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
                    runConvertAllFromUi(state);
                }
                PopButtonStyle();
            }
            ImGui::PopStyleVar();

            ImGui::ProgressBar(state.convertProgress.load(), ImVec2(-1, 0));

            ImGui::Separator();
            if (state.taskDrawerVisible) {
                ImGui::TextUnformatted("任务面板：已展开");
            } else {
                ImGui::TextUnformatted("任务面板：已收起");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(state.taskDrawerVisible ? "收起任务面板" : "展开任务面板")) {
                state.taskDrawerVisible = !state.taskDrawerVisible;
            }
            char brief[96];
            std::snprintf(brief, sizeof(brief), "日志条数：%zu", state.logMessages.size());
            ImGui::TextUnformatted(brief);

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::PopStyleColor(); // ChildBg
}

void runConvertAllFromUi(AppState& state) {
    doConvertAll(state);
}

void runConvertForPaths(AppState& state, const std::vector<std::string>& inputPaths) {
    runConvertForPathsInternal(state, inputPaths);
}
