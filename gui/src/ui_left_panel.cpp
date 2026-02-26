#include "ui_left_panel.h"
#include "ui_main.h"
#include "file_dialogs.h"
#include "utils.h"
#include "win_integration.h"

#include <algorithm>
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
        std::string fullPath = entry.path().string();
        paths.push_back(fullPath);
        std::string relPath = fs::relative(entry.path(), folder).string();
        if (!relPath.empty()) {
            relMap[fullPath] = relPath;
        }
    };

    try {
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(folder))
                processEntry(entry);
        } else {
            for (const auto& entry : fs::directory_iterator(folder))
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
            if (!fs::exists(path)) continue;
            if (fs::is_directory(path)) {
                addFolderFiles(state, path, state.recursive);
                continue;
            }
            if (!isSupportedFile(path)) continue;

            std::string fullPath = fs::absolute(path).string();
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
    auto it = state.relativePathMap.find(inputPath);
    std::string baseName;
    std::string relativeDir;
    if (it != state.relativePathMap.end()) {
        fs::path relPath(it->second);
        baseName = relPath.stem().string();
        relativeDir = relPath.parent_path().string();
        if (relativeDir == ".") relativeDir.clear();
    } else {
        baseName = fs::path(inputPath).stem().string();
    }
    std::string ext = normalizeFormat(format);
    std::string targetDir = relativeDir.empty()
                                ? outputDir
                                : (fs::path(outputDir) / relativeDir).string();

    std::string candidate = (fs::path(targetDir) / (baseName + "." + ext)).string();
    if (overwrite || !fs::exists(candidate)) return candidate;

    int index = 1;
    while (fs::exists(candidate)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "_%d.", index);
        candidate = (fs::path(targetDir) / (baseName + buf + ext)).string();
        ++index;
    }
    return candidate;
}

void doConvertAll(AppState& state) {
    if (state.fileList.empty()) {
        std::string inputDir = state.inputDirBuf;
        if (!inputDir.empty()) {
            addFolderFiles(state, inputDir, state.recursive);
        }
    }

    if (state.fileList.empty()) {
        logMessage(state, "没有可转换的文件");
        return;
    }

    std::string outputDir = state.outputDirBuf;
    if (outputDir.empty()) {
        if (state.fileList.size() == 1) {
            namespace fs = std::filesystem;
            outputDir = fs::path(state.fileList[0]).parent_path().string();
            std::strncpy(state.outputDirBuf, outputDir.c_str(), sizeof(state.outputDirBuf) - 1);
            logMessage(state, "未设置输出目录，已使用源文件目录");
        }
        if (outputDir.empty()) {
            logMessage(state, "请先设置输出目录");
            return;
        }
    }

    const std::string format = normalizedFormatStr(state.outputFormat);
    const bool formatIsBlp = (format == "blp");

    int total = static_cast<int>(state.fileList.size());
    int successCount = 0;

    state.converting = true;
    state.convertProgress = 0.0f;

    for (int i = 0; i < total; ++i) {
        const std::string& inputPath = state.fileList[i];

        RgbaImage image;
        ImageMeta meta;
        std::string error;
        if (!loadImageFile(inputPath, &image, &meta, &error, &state.blpApi)) {
            char msg[512];
            std::snprintf(msg, sizeof(msg), "读取失败：%s（%s）",
                          inputPath.c_str(), error.c_str());
            state.logMessages.push_back(msg);
            state.convertProgress = static_cast<float>(i + 1) / total;
            continue;
        }

        std::string outPath = buildOutputPath(state, inputPath, format, state.overwrite);
        int mipCount = formatIsBlp ? autoMipCount(image.width, image.height) : 1;
        if (!writeImageFile(outPath, format, image, state.quality, mipCount, &error, &state.blpApi)) {
            char msg[512];
            std::snprintf(msg, sizeof(msg), "写入失败：%s（%s）",
                          outPath.c_str(), error.c_str());
            state.logMessages.push_back(msg);
            state.convertProgress = static_cast<float>(i + 1) / total;
            continue;
        }

        ++successCount;
        state.convertProgress = static_cast<float>(i + 1) / total;
    }

    char msg[128];
    std::snprintf(msg, sizeof(msg), "已转换 %d / %d 个文件",
                  successCount, total);
    state.logMessages.push_back(msg);
    state.converting = false;
}

} // namespace

void renderLeftPanel(AppState& state) {
    // File group
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 1));

    ImGui::TextUnformatted("待处理文件");
    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.357f, 0.392f, 0.447f, 1.0f));
    ImGui::TextWrapped("拖拽图片到任意位置，或使用按钮添加。列表中的文件会参与批量转换。");
    ImGui::PopStyleColor();

    // File list
    float listH = ImGui::GetContentRegionAvail().y * 0.28f;
    ImGui::BeginChild("FileList", ImVec2(-1, listH), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(state.fileList.size()); ++i) {
        namespace fs = std::filesystem;
        std::string displayName = fs::path(state.fileList[i]).filename().string();
        bool isSelected = (i == state.selectedFileIndex);
        if (ImGui::Selectable(displayName.c_str(), isSelected)) {
            state.selectedFileIndex = i;
        }
    }
    ImGui::EndChild();

    // File buttons
    if (ImGui::Button("添加文件")) {
        auto files = openFileDialog(state.hwnd,
            L"*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.blp", true);
        std::vector<std::string> paths;
        for (const auto& f : files) paths.push_back(wideToUtf8(f));
        addFiles(state, paths);
    }
    ImGui::SameLine();
    if (ImGui::Button("添加文件夹")) {
        auto folder = openFolderDialog(state.hwnd);
        if (!folder.empty()) {
            std::string folderUtf8 = wideToUtf8(folder);
            std::strncpy(state.inputDirBuf, folderUtf8.c_str(), sizeof(state.inputDirBuf) - 1);
            addFolderFiles(state, folderUtf8, state.recursive);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("移除")) {
        if (state.selectedFileIndex >= 0 && state.selectedFileIndex < static_cast<int>(state.fileList.size())) {
            std::string path = state.fileList[state.selectedFileIndex];
            state.fileSet.erase(path);
            state.relativePathMap.erase(path);
            state.fileList.erase(state.fileList.begin() + state.selectedFileIndex);
            if (state.selectedFileIndex >= static_cast<int>(state.fileList.size())) {
                state.selectedFileIndex = static_cast<int>(state.fileList.size()) - 1;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("清空")) {
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

    ImGui::Spacing();
    ImGui::Separator();

    // Batch paths
    ImGui::TextUnformatted("批量路径");
    ImGui::Spacing();

    ImGui::TextUnformatted("输入目录（可选）：");
    ImGui::PushItemWidth(-80);
    ImGui::InputText("##InputDir", state.inputDirBuf, sizeof(state.inputDirBuf));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("选择##InputBrowse")) {
        auto folder = openFolderDialog(state.hwnd);
        if (!folder.empty()) {
            std::strncpy(state.inputDirBuf, wideToUtf8(folder).c_str(), sizeof(state.inputDirBuf) - 1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("扫描")) {
        std::string dir = state.inputDirBuf;
        if (!dir.empty()) {
            namespace fs = std::filesystem;
            if (fs::exists(dir) && fs::is_directory(dir)) {
                addFolderFiles(state, dir, state.recursive);
            } else {
                logMessage(state, "输入目录不存在");
            }
        } else {
            logMessage(state, "请先设置输入目录");
        }
    }

    ImGui::TextUnformatted("输出目录（必填）：");
    ImGui::PushItemWidth(-80);
    ImGui::InputText("##OutputDir", state.outputDirBuf, sizeof(state.outputDirBuf));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("选择##OutputBrowse")) {
        auto folder = openFolderDialog(state.hwnd);
        if (!folder.empty()) {
            std::strncpy(state.outputDirBuf, wideToUtf8(folder).c_str(), sizeof(state.outputDirBuf) - 1);
        }
    }

    ImGui::Checkbox("包含子目录", &state.recursive);

    ImGui::Spacing();
    ImGui::Separator();

    // Convert settings
    ImGui::TextUnformatted("转换设置");
    ImGui::Spacing();

    ImGui::TextUnformatted("输出格式：");
    ImGui::RadioButton("BLP", &state.outputFormat, 0); ImGui::SameLine();
    ImGui::RadioButton("PNG", &state.outputFormat, 1); ImGui::SameLine();
    ImGui::RadioButton("JPG", &state.outputFormat, 2);
    ImGui::RadioButton("BMP", &state.outputFormat, 3); ImGui::SameLine();
    ImGui::RadioButton("TGA", &state.outputFormat, 4);

    bool qualityEnabled = (state.outputFormat == 0 || state.outputFormat == 2);
    const char* qualityLabel = (state.outputFormat == 0) ? "BLP 质量："
                             : (state.outputFormat == 2) ? "JPG 质量："
                             : "质量：";
    ImGui::TextUnformatted(qualityLabel);
    ImGui::SameLine();
    if (!qualityEnabled) ImGui::BeginDisabled();
    ImGui::PushItemWidth(-60);
    ImGui::SliderInt("##Quality", &state.quality, 0, 100);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    char qualityText[8];
    std::snprintf(qualityText, sizeof(qualityText), "%d", state.quality);
    ImGui::TextUnformatted(qualityText);
    if (!qualityEnabled) ImGui::EndDisabled();

    ImGui::Checkbox("覆盖已存在文件", &state.overwrite);

    ImGui::Spacing();

    // Convert button
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
    if (state.converting) {
        ImGui::BeginDisabled();
        ImGui::Button("转换中...", ImVec2(-1, 0));
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("开始转换", ImVec2(-1, 0))) {
            doConvertAll(state);
        }
    }
    ImGui::PopStyleVar();

    // Progress bar
    ImGui::ProgressBar(state.convertProgress.load(), ImVec2(-1, 0));

    ImGui::Spacing();
    ImGui::Separator();

    // Log
    ImGui::TextUnformatted("日志");
    float logH = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("LogArea", ImVec2(-1, logH), ImGuiChildFlags_Borders);
    for (const auto& msg : state.logMessages) {
        ImGui::TextWrapped("%s", msg.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::PopStyleColor(); // ChildBg
}
