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
}

void clearFileList(AppState& state) {
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

    for (const auto& pathUtf8 : paths) {
        fs::path path = fsPathFromUtf8(pathUtf8);
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) continue;

        if (fs::is_directory(path, ec) && !ec) {
            addFolder(state, path, state.recursive);
            continue;
        }

        addSingleFile(state, path, nullptr);
    }

    if (static_cast<int>(state.fileList.size()) > beforeCount) {
        state.selectedFileIndex = static_cast<int>(state.fileList.size()) - 1;
    } else if (!state.fileList.empty() && state.selectedFileIndex < 0) {
        state.selectedFileIndex = 0;
    }
}

void showAboutDialog(AppState& state) {
    if (state.showAboutPopup) {
        ImGui::OpenPopup("关于 BLP 查看器");
        state.showAboutPopup = false;
    }

    if (ImGui::BeginPopupModal("关于 BLP 查看器", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("BLP Viewer");
        ImGui::Separator();
        ImGui::TextUnformatted("用于 BLP / PNG / JPG / BMP / TGA 的查看与转换工具。");
        ImGui::Text("BLP 库状态：%s", state.blpApi.isLoaded() ? "已加载" : "未加载");
        if (state.blpApi.isLoaded()) {
            const std::string ver = state.blpApi.version();
            if (!ver.empty()) {
                ImGui::Text("版本：%s", ver.c_str());
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
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("视图")) {
        ImGui::MenuItem("显示任务面板", nullptr, &state.taskDrawerVisible);
        if (ImGui::MenuItem("重置布局")) {
            state.leftPanelWidth = 320.0f;
            state.taskDrawerHeight = 170.0f;
            state.taskDrawerVisible = false;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("工具")) {
        if (ImGui::MenuItem(state.blpAssociated ? "BLP 打开方式：已关联" : "关联 BLP 打开方式")) {
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

        std::wstring dllPath = getAppDir() + L"\\blp_thumbnail.dll";
        bool dllExists = std::filesystem::exists(dllPath);
        bool checked = state.thumbnailRegistered;
        if (ImGui::MenuItem("资源管理器缩略图（BLP/TGA）", nullptr, checked, dllExists)) {
            std::string error;
            const char* entry = checked ? "DllUnregisterServer" : "DllRegisterServer";
            if (callDllEntry(dllPath, entry, &error)) {
                state.logMessages.push_back(checked
                    ? "已关闭资源管理器缩略图（BLP/TGA）"
                    : "已启用资源管理器缩略图（BLP/TGA）");
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

void renderTaskDrawer(AppState& state) {
    if (!state.taskDrawerVisible) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.961f, 0.965f, 0.973f, 1.0f));
    ImGui::BeginChild("TaskDrawer", ImVec2(0, state.taskDrawerHeight), ImGuiChildFlags_Borders);

    int failedCount = 0;
    for (const auto& item : state.lastConvertResults) {
        if (!item.success) ++failedCount;
    }

    if (ImGui::BeginTabBar("TaskTabs")) {
        if (ImGui::BeginTabItem("任务进度")) {
            const float progress = std::clamp(state.convertProgress.load(), 0.0f, 1.0f);
            ImGui::TextUnformatted(state.converting.load() ? "当前状态：转换中" : "当前状态：空闲");
            ImGui::ProgressBar(progress, ImVec2(-1, 0));

            char summary[192];
            std::snprintf(summary, sizeof(summary),
                          "本次结果：总计 %d | 成功 %d | 失败 %d | 日志 %zu",
                          state.lastConvertTotal, state.lastConvertSuccess, state.lastConvertFailed,
                          state.logMessages.size());
            ImGui::TextUnformatted(summary);
            if (state.lastConvertFailed > 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, UiColorError());
                ImGui::Text("失败项：%d", state.lastConvertFailed);
                ImGui::PopStyleColor();
            } else if (state.lastConvertSuccess > 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, UiColorSuccess());
                ImGui::Text("最近一次转换全部成功");
                ImGui::PopStyleColor();
            }

            if (failedCount > 0 && !state.converting.load()) {
                PushPrimaryButtonStyle();
                if (ImGui::Button("重试失败项")) {
                    std::vector<std::string> retryPaths;
                    std::unordered_set<std::string> dedup;
                    for (const auto& item : state.lastConvertResults) {
                        if (item.success) continue;
                        if (item.inputPath.empty()) continue;
                        if (dedup.insert(item.inputPath).second) {
                            retryPaths.push_back(item.inputPath);
                        }
                    }
                    if (!retryPaths.empty()) {
                        runConvertForPaths(state, retryPaths);
                    }
                }
                PopButtonStyle();
                ImGui::SameLine();
            }

            PushSecondaryButtonStyle();
            if (ImGui::Button("清空结果")) {
                state.lastConvertResults.clear();
                state.lastConvertTotal = 0;
                state.lastConvertSuccess = 0;
                state.lastConvertFailed = 0;
            }
            PopButtonStyle();
            ImGui::SameLine();
            PushDangerButtonStyle();
            if (ImGui::Button("清空日志")) {
                state.logMessages.clear();
            }
            PopButtonStyle();

            ImGui::BeginChild("TaskResultList", ImVec2(0, 0), ImGuiChildFlags_Borders);
            if (state.lastConvertResults.empty()) {
                ImGui::TextUnformatted("暂无任务结果");
            } else {
                for (const auto& item : state.lastConvertResults) {
                    if (item.success) {
                        ImGui::PushStyleColor(ImGuiCol_Text, UiColorSuccess());
                        ImGui::Text("[成功] %s", item.inputPath.c_str());
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, UiColorError());
                        ImGui::Text("[失败] %s", item.inputPath.c_str());
                        ImGui::PopStyleColor();
                        if (!item.error.empty()) {
                            ImGui::TextWrapped("     %s", item.error.c_str());
                        }
                    }
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("日志")) {
            if (state.logMessages.empty()) {
                ImGui::TextUnformatted("暂无日志");
            } else {
                ImGui::BeginChild("TaskLogList", ImVec2(0, 0), ImGuiChildFlags_Borders);
                const bool keepBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
                for (const auto& msg : state.logMessages) {
                    ImGui::TextWrapped("%s", msg.c_str());
                }
                if (keepBottom) {
                    ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("错误")) {
            ImGui::BeginChild("ErrorList", ImVec2(0, 0), ImGuiChildFlags_Borders);
            int shown = 0;
            for (const auto& item : state.lastConvertResults) {
                if (item.success) continue;
                ++shown;
                ImGui::PushStyleColor(ImGuiCol_Text, UiColorError());
                ImGui::TextUnformatted(item.inputPath.c_str());
                ImGui::PopStyleColor();
                if (!item.outputPath.empty()) {
                    ImGui::TextWrapped("输出：%s", item.outputPath.c_str());
                }
                ImGui::TextWrapped("原因：%s", item.error.empty() ? "未知错误" : item.error.c_str());
                ImGui::Separator();
            }
            if (shown == 0) {
                ImGui::TextUnformatted("未检测到失败项");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
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
    const float minDrawerH = compactLayout ? (96.0f * state.dpiScale) : (120.0f * state.dpiScale);
    const float maxDrawerH = compactLayout ? (240.0f * state.dpiScale) : (420.0f * state.dpiScale);
    if (state.taskDrawerVisible) {
        state.taskDrawerHeight = std::clamp(state.taskDrawerHeight, minDrawerH, maxDrawerH);
    }

    const float drawerSplitterH = state.taskDrawerVisible ? (5.0f * state.dpiScale) : 0.0f;
    float drawerH = state.taskDrawerVisible ? state.taskDrawerHeight : 0.0f;
    const float minWorkspaceH = compactLayout ? (220.0f * state.dpiScale) : (180.0f * state.dpiScale);
    float contentH = ImGui::GetContentRegionAvail().y - statusBarH - drawerSplitterH - drawerH;
    if (state.taskDrawerVisible && contentH < minWorkspaceH) {
        drawerH = std::max(minDrawerH, drawerH - (minWorkspaceH - contentH));
        state.taskDrawerHeight = drawerH;
        contentH = ImGui::GetContentRegionAvail().y - statusBarH - drawerSplitterH - drawerH;
    }
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

    if (state.taskDrawerVisible) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.843f, 0.859f, 0.890f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.176f, 0.424f, 0.875f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.176f, 0.424f, 0.875f, 0.8f));
        ImGui::Button("##TaskDrawerSplitter", ImVec2(-1, drawerSplitterH));
        if (ImGui::IsItemActive()) {
            state.taskDrawerHeight = std::clamp(
                state.taskDrawerHeight - ImGui::GetIO().MouseDelta.y,
                minDrawerH, maxDrawerH);
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }
        ImGui::PopStyleColor(3);

        renderTaskDrawer(state);
    }

    renderStatusBar(state);

    ImGui::End();
}
