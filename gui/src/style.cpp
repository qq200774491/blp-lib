#include "style.h"

#include <cstdio>
#include <filesystem>

void ApplyImGuiStyle(float dpiScale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;

    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 12.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]           = ImVec4(0.961f, 0.965f, 0.973f, 1.00f); // #f5f6f8
    colors[ImGuiCol_ChildBg]            = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);
    colors[ImGuiCol_PopupBg]            = ImVec4(1.000f, 1.000f, 1.000f, 0.98f);
    colors[ImGuiCol_Border]             = ImVec4(0.820f, 0.835f, 0.863f, 1.00f); // #d1d5dc
    colors[ImGuiCol_FrameBg]            = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.933f, 0.945f, 0.965f, 1.00f); // #eef2f8
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.871f, 0.890f, 0.929f, 1.00f); // #dee3ed
    colors[ImGuiCol_TitleBg]            = ImVec4(0.933f, 0.941f, 0.957f, 1.00f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.933f, 0.941f, 0.957f, 1.00f);
    colors[ImGuiCol_MenuBarBg]          = ImVec4(0.961f, 0.965f, 0.973f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.914f, 0.929f, 0.953f, 1.00f); // #e9edf3
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.714f, 0.753f, 0.804f, 1.00f); // #b6c0cd
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.604f, 0.651f, 0.706f, 1.00f); // #9aa6b4
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.604f, 0.651f, 0.706f, 1.00f);
    colors[ImGuiCol_CheckMark]          = ImVec4(0.176f, 0.424f, 0.875f, 1.00f); // #2d6cdf
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.176f, 0.424f, 0.875f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.141f, 0.369f, 0.761f, 1.00f); // #245ec2
    colors[ImGuiCol_Button]             = ImVec4(0.176f, 0.424f, 0.875f, 1.00f); // #2d6cdf
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.141f, 0.369f, 0.761f, 1.00f); // #245ec2
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.122f, 0.322f, 0.675f, 1.00f);
    colors[ImGuiCol_Header]             = ImVec4(0.933f, 0.945f, 0.965f, 1.00f); // #eef2f8
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.871f, 0.890f, 0.929f, 1.00f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.176f, 0.424f, 0.875f, 0.31f);
    colors[ImGuiCol_Separator]          = ImVec4(0.843f, 0.859f, 0.890f, 1.00f); // #d7dbe3
    colors[ImGuiCol_Tab]               = ImVec4(0.933f, 0.945f, 0.965f, 1.00f);
    colors[ImGuiCol_TabHovered]        = ImVec4(0.176f, 0.424f, 0.875f, 0.80f);
    colors[ImGuiCol_TabSelected]       = ImVec4(0.176f, 0.424f, 0.875f, 1.00f);
    colors[ImGuiCol_ResizeGrip]         = ImVec4(0.176f, 0.424f, 0.875f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]  = ImVec4(0.176f, 0.424f, 0.875f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]   = ImVec4(0.176f, 0.424f, 0.875f, 0.95f);
    colors[ImGuiCol_PlotHistogram]      = ImVec4(0.176f, 0.424f, 0.875f, 1.00f);
    colors[ImGuiCol_Text]              = ImVec4(0.118f, 0.129f, 0.153f, 1.00f); // #1e2127
    colors[ImGuiCol_TextDisabled]      = ImVec4(0.659f, 0.690f, 0.737f, 1.00f); // #a8b0bc

    style.ScaleAllSizes(dpiScale);
}

void LoadFonts(float dpiScale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float fontSize = 16.0f * dpiScale;

    // Try to load Microsoft YaHei for Chinese text support
    const char* fontPaths[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
    };

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;

    bool fontLoaded = false;
    for (const char* fontPath : fontPaths) {
        if (std::filesystem::exists(fontPath)) {
            ImFont* font = io.Fonts->AddFontFromFileTTF(
                fontPath, fontSize, &config,
                io.Fonts->GetGlyphRangesChineseFull());
            if (font) {
                fontLoaded = true;
                break;
            }
        }
    }

    if (!fontLoaded) {
        // Fallback to default font
        io.Fonts->AddFontDefault();
    }

    io.Fonts->Build();
}
