#include "ui_main.h"
#include "ui_left_panel.h"
#include "ui_right_panel.h"
#include "win_integration.h"
#include "file_dialogs.h"
#include "utils.h"

#include <cstdio>
#include <filesystem>

void renderToolbar(AppState& state) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.902f, 0.922f, 0.953f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.863f, 0.890f, 0.933f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.357f, 0.392f, 0.447f, 1.0f));

    if (state.blpAssociated) {
        ImGui::BeginDisabled();
        ImGui::Button("BLP 已关联");
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("关联 BLP 打开方式")) {
            std::string error;
            std::wstring appPath = getAppPath();
            if (registerBlpAssociation(appPath, &error)) {
                state.blpAssociated = isBlpAssociated();
                if (state.blpAssociated) {
                    state.logMessages.push_back("已关联 .blp 打开方式");
                } else {
                    state.logMessages.push_back("已写入关联项，但系统默认应用未生效");
                }
            } else {
                char msg[256];
                std::snprintf(msg, sizeof(msg), "关联 .blp 失败：%s", error.c_str());
                state.logMessages.push_back(msg);
            }
        }
    }

    ImGui::SameLine();

    // Thumbnail toggle
    std::wstring dllPath = getAppDir() + L"\\blp_thumbnail.dll";
    bool dllExists = std::filesystem::exists(dllPath);
    if (!dllExists) {
        ImGui::BeginDisabled();
        ImGui::Button("资源管理器缩略图（缺少 DLL）");
        ImGui::EndDisabled();
    } else {
        bool checked = state.thumbnailRegistered;
        if (ImGui::Checkbox(state.thumbnailRegistered
                            ? "资源管理器缩略图已启用（BLP/TGA）"
                            : "资源管理器缩略图（BLP/TGA）",
                            &checked)) {
            std::string error;
            const char* entry = checked ? "DllRegisterServer" : "DllUnregisterServer";
            if (callDllEntry(dllPath, entry, &error)) {
                state.logMessages.push_back(checked
                    ? "已启用资源管理器缩略图（BLP/TGA）"
                    : "已关闭资源管理器缩略图（BLP/TGA）");
            } else {
                char msg[256];
                std::snprintf(msg, sizeof(msg), "%s：%s",
                              checked ? "启用缩略图失败"
                                      : "关闭缩略图失败",
                              error.c_str());
                state.logMessages.push_back(msg);
            }
            state.thumbnailRegistered = isThumbnailRegistered();
        }
    }

    ImGui::PopStyleColor(4);
}

void renderStatusBar(AppState& state) {
    float statusH = 22.0f * state.dpiScale;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - statusH - ImGui::GetStyle().WindowPadding.y);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.933f, 0.941f, 0.957f, 1.0f));
    ImGui::BeginChild("StatusBar", ImVec2(0, statusH), ImGuiChildFlags_None);

    // Info text
    if (state.currentMeta.width > 0) {
        char info[256];
        std::snprintf(info, sizeof(info), "%d x %d 像素 | %s | %s",
                      state.currentMeta.width, state.currentMeta.height,
                      formatFileSize(state.currentMeta.fileSize).c_str(),
                      state.currentMeta.format.c_str());
        ImGui::TextUnformatted(info);
    } else {
        ImGui::TextUnformatted("未加载图像");
    }

    // Zoom percentage
    ImGui::SameLine(ImGui::GetWindowWidth() - 250 * state.dpiScale);
    char zoomText[32];
    std::snprintf(zoomText, sizeof(zoomText), "缩放：%d%%", state.zoomPercent);
    ImGui::TextUnformatted(zoomText);

    // BLP version
    ImGui::SameLine(ImGui::GetWindowWidth() - 120 * state.dpiScale);
    if (state.blpApi.isLoaded()) {
        std::string ver = state.blpApi.version();
        char blpText[64];
        std::snprintf(blpText, sizeof(blpText), "BLP：%s",
                      ver.empty() ? "已加载" : ver.c_str());
        ImGui::TextUnformatted(blpText);
    } else {
        ImGui::TextUnformatted("BLP：未加载");
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
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Toolbar
    renderToolbar(state);
    ImGui::Separator();

    float contentH = ImGui::GetContentRegionAvail().y - 30.0f * state.dpiScale;

    // Left panel
    ImGui::BeginChild("LeftPanel", ImVec2(state.leftPanelWidth, contentH), ImGuiChildFlags_None);
    renderLeftPanel(state);
    ImGui::EndChild();

    ImGui::SameLine();

    // Splitter
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.843f, 0.859f, 0.890f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.176f, 0.424f, 0.875f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.176f, 0.424f, 0.875f, 0.8f));
    ImGui::Button("##Splitter", ImVec2(6.0f, contentH));
    if (ImGui::IsItemActive()) {
        float delta = ImGui::GetIO().MouseDelta.x;
        state.leftPanelWidth += delta;
        state.leftPanelWidth = std::clamp(state.leftPanelWidth, 280.0f, 500.0f);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    // Right panel
    float rightW = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("RightPanel", ImVec2(rightW, contentH), ImGuiChildFlags_None);
    renderRightPanel(state);
    ImGui::EndChild();

    // Status bar
    renderStatusBar(state);

    ImGui::End();
}
