#include "ui_main.h"
#include "ui_left_panel.h"
#include "ui_right_panel.h"
#include "win_integration.h"
#include "file_dialogs.h"
#include "style.h"
#include "utils.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), sz, nullptr, nullptr);
    return result;
}

void removeSelectedFile(AppState& state) {
    if (state.selectedFileIndex < 0 ||
        state.selectedFileIndex >= static_cast<int>(state.fileList.size())) {
        return;
    }

    const std::string path = state.fileList[state.selectedFileIndex];
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
        return;
    }

    if (state.selectedFileIndex >= static_cast<int>(state.fileList.size())) {
        state.selectedFileIndex = static_cast<int>(state.fileList.size()) - 1;
    }
    if (removedCurrentPreview) {
        state.currentPreviewPath.clear();
    }
}

void clearFileList(AppState& state) {
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

void addSingleFile(AppState& state,
                   const std::filesystem::path& filePath,
                   const std::filesystem::path* relativeBase) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(filePath, ec) || ec) return;

    const std::string fullPath = fsPathToUtf8(fs::absolute(filePath, ec));
    if (ec || fullPath.empty()) return;
    if (!isSupportedFile(fullPath)) return;
    if (state.fileSet.count(fullPath)) return;

    state.fileList.push_back(fullPath);
    state.fileSet.insert(fullPath);

    if (relativeBase) {
        std::error_code relEc;
        const fs::path rel = fs::relative(filePath, *relativeBase, relEc);
        if (!relEc) {
            const std::string relPath = fsPathToUtf8(rel);
            if (!relPath.empty() && relPath != ".") {
                state.relativePathMap[fullPath] = relPath;
            }
        }
    }
}

void addFolder(AppState& state, const std::filesystem::path& folder, bool recursive) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(folder, ec) || ec) return;
    if (!fs::is_directory(folder, ec) || ec) return;

    if (recursive) {
        fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        while (!ec && it != end) {
            addSingleFile(state, it->path(), &folder);
            it.increment(ec);
        }
    } else {
        fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
        fs::directory_iterator end;
        while (!ec && it != end) {
            addSingleFile(state, it->path(), &folder);
            it.increment(ec);
        }
    }
}

void addPaths(AppState& state, const std::vector<std::string>& paths) {
    namespace fs = std::filesystem;
    const int beforeCount = static_cast<int>(state.fileList.size());
    bool addedViaFolder = false;

    for (const auto& pathUtf8 : paths) {
        fs::path path = fsPathFromUtf8(pathUtf8);
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) continue;

        if (fs::is_directory(path, ec) && !ec) {
            addedViaFolder = true;
            addFolder(state, path, state.recursive);
            continue;
        }

        addSingleFile(state, path, nullptr);
    }

    const int added = static_cast<int>(state.fileList.size()) - beforeCount;
    if (added > 0) {
        state.selectedFileIndex = static_cast<int>(state.fileList.size()) - 1;
        // Single file opened -> 预览; batch add (folder or multiple) -> 网格.
        state.rightViewMode = (addedViaFolder || added > 1) ? 0 : 1;
    } else if (!state.fileList.empty() && state.selectedFileIndex < 0) {
        state.selectedFileIndex = 0;
    }
}

void showAboutDialog(AppState& state) {
    if (state.showAboutPopup) {
        ImGui::OpenPopup("关于 图像快速处理工具");
        state.showAboutPopup = false;
    }

    if (ImGui::BeginPopupModal("关于 图像快速处理工具", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("图像快速处理工具 1.4v - 小为");
        ImGui::Separator();
        ImGui::TextUnformatted("版本：1.4");
        ImGui::TextUnformatted("作者：小为");
        ImGui::TextUnformatted("用于 BLP / PNG / JPG / BMP / TGA 的查看与转换工具。");
        ImGui::Text("BLP 库状态：%s", state.blpApi.isLoaded() ? "已加载" : "未加载");
        if (state.blpApi.isLoaded()) {
            const std::string ver = state.blpApi.version();
            if (!ver.empty()) {
                ImGui::Text("BLP 库版本：%s", ver.c_str());
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("关闭")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace

void renderMenuBar(AppState& state) {
    if (!ImGui::BeginMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("文件")) {
        if (ImGui::MenuItem("添加文件...")) {
            auto files = openFileDialog(state.hwnd, L"*.blp;*.png;*.jpg;*.jpeg;*.bmp;*.tga", true);
            std::vector<std::string> paths;
            paths.reserve(files.size());
            for (const auto& file : files) {
                paths.push_back(wideToUtf8(file));
            }
            addPaths(state, paths);
        }

        if (ImGui::MenuItem("添加文件夹...")) {
            const std::wstring folder = openFolderDialog(state.hwnd);
            if (!folder.empty()) {
                const std::string folderUtf8 = wideToUtf8(folder);
                std::strncpy(state.inputDirBuf, folderUtf8.c_str(), sizeof(state.inputDirBuf) - 1);
                addPaths(state, {folderUtf8});
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem("移除选中", nullptr, false,
                            state.selectedFileIndex >= 0 &&
                            state.selectedFileIndex < static_cast<int>(state.fileList.size()))) {
            removeSelectedFile(state);
        }
        if (ImGui::MenuItem("清空列表", nullptr, false, !state.fileList.empty())) {
            clearFileList(state);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("退出")) {
            PostMessageW(state.hwnd, WM_CLOSE, 0, 0);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("编辑")) {
        if (ImGui::MenuItem("清空日志", nullptr, false, !state.logMessages.empty())) {
            state.logMessages.clear();
        }
        ImGui::Separator();
        if (state.outputFormat == 0) {
            ImGui::TextDisabled("当前输出格式为 BLP，质量可在左侧直接调整");
        } else {
            ImGui::TextUnformatted("非 BLP 输出质量");
            ImGui::SetNextItemWidth(160.0f * state.dpiScale);
            ImGui::SliderInt("质量##NonBlpQuality", &state.quality, 0, 100);
            if (ImGui::MenuItem("重置为 100%")) {
                state.quality = 100;
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("视图")) {
        if (ImGui::MenuItem("重置布局")) {
            state.leftPanelWidth = 320.0f;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("工具")) {
        if (ImGui::MenuItem(state.blpAssociated ? "默认打开 .blp：已设置" : "设置默认打开 .blp")) {
            std::string error;
            std::wstring appPath = getAppPath();
            if (registerBlpAssociation(appPath, &error)) {
                state.blpAssociated = isBlpAssociated();
                state.logMessages.push_back(state.blpAssociated
                    ? "已关联 .blp 打开方式"
                    : "已写入关联项，但系统默认应用未生效");
            } else {
                char msg[256];
                std::snprintf(msg, sizeof(msg), "关联 .blp 失败：%s", error.c_str());
                state.logMessages.push_back(msg);
            }
        }
        if (ImGui::MenuItem(state.pngAssociated ? "默认打开 .png：已设置" : "设置默认打开 .png")) {
            std::string error;
            std::wstring appPath = getAppPath();
            if (registerPngAssociation(appPath, &error)) {
                state.pngAssociated = isPngAssociated();
                state.logMessages.push_back(state.pngAssociated
                    ? "已关联 .png 打开方式"
                    : "已写入关联项，但系统默认应用未生效");
            } else {
                char msg[256];
                std::snprintf(msg, sizeof(msg), "关联 .png 失败：%s", error.c_str());
                state.logMessages.push_back(msg);
            }
        }
        if (ImGui::MenuItem(state.tgaAssociated ? "默认打开 .tga：已设置" : "设置默认打开 .tga")) {
            std::string error;
            std::wstring appPath = getAppPath();
            if (registerTgaAssociation(appPath, &error)) {
                state.tgaAssociated = isTgaAssociated();
                state.logMessages.push_back(state.tgaAssociated
                    ? "已关联 .tga 打开方式"
                    : "已写入关联项，但系统默认应用未生效");
            } else {
                char msg[256];
                std::snprintf(msg, sizeof(msg), "关联 .tga 失败：%s", error.c_str());
                state.logMessages.push_back(msg);
            }
        }

        ImGui::Separator();
        std::wstring dllPath = getAppDir() + L"\\blp_thumbnail.dll";
        bool dllExists = std::filesystem::exists(dllPath);
        bool checked = state.thumbnailRegistered;
        if (ImGui::MenuItem("资源管理器缩略图（BLP/TGA/PNG）", nullptr, checked, dllExists)) {
            std::string error;
            const char* entry = checked ? "DllUnregisterServer" : "DllRegisterServer";
            if (callDllEntry(dllPath, entry, &error)) {
                state.logMessages.push_back(checked
                    ? "已关闭资源管理器缩略图（BLP/TGA/PNG）"
                    : "已启用资源管理器缩略图（BLP/TGA/PNG）");
            } else {
                char msg[256];
                std::snprintf(msg, sizeof(msg), "%s：%s",
                              checked ? "关闭缩略图失败" : "启用缩略图失败",
                              error.c_str());
                state.logMessages.push_back(msg);
            }
            state.thumbnailRegistered = isThumbnailRegistered();
        }

        if (ImGui::MenuItem("重新检测系统状态")) {
            state.blpAssociated = isBlpAssociated();
            state.pngAssociated = isPngAssociated();
            state.tgaAssociated = isTgaAssociated();
            state.thumbnailRegistered = isThumbnailRegistered();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("帮助")) {
        if (ImGui::MenuItem("关于")) {
            state.showAboutPopup = true;
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
    showAboutDialog(state);
}

void renderStatusBar(AppState& state) {
    float statusH = 22.0f * state.dpiScale;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - statusH - ImGui::GetStyle().WindowPadding.y);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.933f, 0.941f, 0.957f, 1.0f));
    ImGui::BeginChild("StatusBar", ImVec2(0, statusH), ImGuiChildFlags_None);

    std::string leftInfo;
    if (state.currentMeta.width > 0) {
        char info[256];
        std::snprintf(info, sizeof(info), "%d x %d 像素 | %s | %s",
                      state.currentMeta.width, state.currentMeta.height,
                      formatFileSize(state.currentMeta.fileSize).c_str(),
                      state.currentMeta.format.c_str());
        leftInfo = info;
    } else {
        leftInfo = "未加载图像";
    }

    struct StatusSegment {
        std::string text;
        bool colored = false;
        ImVec4 color = ImVec4(0, 0, 0, 1);
    };

    std::vector<StatusSegment> rightSegments;
    rightSegments.reserve(3);

    char zoomText[32];
    std::snprintf(zoomText, sizeof(zoomText), "缩放：%d%%", state.zoomPercent);
    rightSegments.push_back({zoomText});

    if (state.lastConvertFailed > 0) {
        char failText[48];
        std::snprintf(failText, sizeof(failText), "失败：%d", state.lastConvertFailed);
        rightSegments.push_back({failText, true, UiColorError()});
    } else if (state.lastConvertSuccess > 0) {
        char okText[48];
        std::snprintf(okText, sizeof(okText), "成功：%d", state.lastConvertSuccess);
        rightSegments.push_back({okText, true, UiColorSuccess()});
    }

    std::string blpSegment;
    if (state.blpApi.isLoaded()) {
        std::string ver = state.blpApi.version();
        char blpBuf[64];
        std::snprintf(blpBuf, sizeof(blpBuf), "BLP：%s",
                      ver.empty() ? "已加载" : ver.c_str());
        blpSegment = blpBuf;
    } else {
        blpSegment = "BLP：未加载";
    }
    rightSegments.push_back({blpSegment});

    const float rowY = ImGui::GetCursorPosY();
    const float contentW = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    float rightWidth = 0.0f;
    for (size_t i = 0; i < rightSegments.size(); ++i) {
        rightWidth += ImGui::CalcTextSize(rightSegments[i].text.c_str()).x;
        if (i + 1 < rightSegments.size()) {
            rightWidth += spacing;
        }
    }

    float rightStartX = std::max(0.0f, contentW - rightWidth);
    float leftMaxWidth = std::max(0.0f, rightStartX - spacing);

    const ImVec2 leftPos = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(
        leftPos,
        ImVec2(leftPos.x + leftMaxWidth, leftPos.y + ImGui::GetTextLineHeightWithSpacing()),
        true);
    ImGui::TextUnformatted(leftInfo.c_str());
    ImGui::PopClipRect();

    ImGui::SetCursorPos(ImVec2(rightStartX, rowY));
    for (size_t i = 0; i < rightSegments.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, spacing);
        }
        if (rightSegments[i].colored) {
            ImGui::PushStyleColor(ImGuiCol_Text, rightSegments[i].color);
        }
        ImGui::TextUnformatted(rightSegments[i].text.c_str());
        if (rightSegments[i].colored) {
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void renderMainUI(AppState& state) {
    // Full-window ImGui panel
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGui::Begin("##MainWindow", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_MenuBar);

    renderMenuBar(state);

    const float statusBarH = 22.0f * state.dpiScale;
    const bool compactLayout = viewport->WorkSize.y <= 780.0f * state.dpiScale;
    const float minWorkspaceH = compactLayout ? (220.0f * state.dpiScale) : (180.0f * state.dpiScale);
    float contentH = ImGui::GetContentRegionAvail().y - statusBarH;
    contentH = std::max(contentH, minWorkspaceH);

    ImGui::BeginChild("WorkspaceRoot", ImVec2(0, contentH), ImGuiChildFlags_None);
    const float paneHeight = ImGui::GetContentRegionAvail().y;
    const float minLeftW = compactLayout ? (250.0f * state.dpiScale) : (280.0f * state.dpiScale);
    const float rightReserveW = compactLayout ? (220.0f * state.dpiScale) : (260.0f * state.dpiScale);
    const float maxLeftW = std::max(minLeftW, ImGui::GetContentRegionAvail().x - rightReserveW);
    state.leftPanelWidth = std::clamp(state.leftPanelWidth, minLeftW, maxLeftW);

    ImGui::BeginChild("LeftPanel", ImVec2(state.leftPanelWidth, paneHeight), ImGuiChildFlags_None);
    renderLeftPanel(state);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.843f, 0.859f, 0.890f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.176f, 0.424f, 0.875f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.176f, 0.424f, 0.875f, 0.8f));
    ImGui::Button("##MainSplitter", ImVec2(6.0f * state.dpiScale, paneHeight));
    if (ImGui::IsItemActive()) {
        state.leftPanelWidth += ImGui::GetIO().MouseDelta.x;
        state.leftPanelWidth = std::clamp(state.leftPanelWidth, minLeftW, maxLeftW);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    ImGui::BeginChild("RightPanel", ImVec2(0, paneHeight), ImGuiChildFlags_None);
    renderRightPanel(state);
    ImGui::EndChild();
    ImGui::EndChild();

    renderStatusBar(state);

    ImGui::End();
}
