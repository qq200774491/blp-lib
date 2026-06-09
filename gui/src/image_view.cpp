#include "image_view.h"

#include <algorithm>
#include <cmath>

namespace {

void drawCheckerboard(ImDrawList* drawList, const ImVec2& minPos, const ImVec2& maxPos, float cellSize) {
    if (!drawList || cellSize <= 1.0f) return;
    if (maxPos.x <= minPos.x || maxPos.y <= minPos.y) return;

    const ImU32 light = IM_COL32(236, 240, 246, 255);
    const ImU32 dark = IM_COL32(208, 215, 226, 255);

    int row = 0;
    for (float y = minPos.y; y < maxPos.y; y += cellSize, ++row) {
        int col = 0;
        const float y2 = std::min(y + cellSize, maxPos.y);
        for (float x = minPos.x; x < maxPos.x; x += cellSize, ++col) {
            const float x2 = std::min(x + cellSize, maxPos.x);
            const ImU32 color = ((row + col) & 1) ? dark : light;
            drawList->AddRectFilled(ImVec2(x, y), ImVec2(x2, y2), color);
        }
    }
}

} // namespace

void ImageViewer::init(ID3D11Device* device) {
    checkerboard = ImageTexture::createCheckerboard(device);
}

void ImageViewer::setImage(ID3D11Device* device, const uint8_t* rgba, int w, int h) {
    texture.release();
    texture.createFromRGBA(device, rgba, w, h);
    imageWidth = w;
    imageHeight = h;
    hasImage = true;
    zoom = 1.0f;
    panX = 0.0f;
    panY = 0.0f;
    fitMode = false;
}

void ImageViewer::clearImage() {
    texture.release();
    hasImage = false;
    imageWidth = 0;
    imageHeight = 0;
    zoom = 1.0f;
    panX = 0.0f;
    panY = 0.0f;
    fitMode = true;
}

void ImageViewer::fitToView(float viewW, float viewH) {
    if (!hasImage || imageWidth <= 0 || imageHeight <= 0) return;
    float scaleX = viewW / static_cast<float>(imageWidth);
    float scaleY = viewH / static_cast<float>(imageHeight);
    zoom = std::min(scaleX, scaleY);
    panX = 0.0f;
    panY = 0.0f;
    fitMode = true;
}

void ImageViewer::setZoom(float value, float pivotX, float pivotY) {
    zoom = std::clamp(value, 0.05f, 20.0f);
    fitMode = false;
}

void ImageViewer::render(float viewW, float viewH) {
    if (!hasImage || !texture.isValid()) {
        // Placeholder text
        ImVec2 textSize = ImGui::CalcTextSize("拖放图片到这里预览");
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        float textX = cursorPos.x + (viewW - textSize.x) * 0.5f;
        float textY = cursorPos.y + (viewH - textSize.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(textX, textY),
            IM_COL32(90, 96, 110, 255),
            "拖放图片到这里预览");
        return;
    }

    float displayW = imageWidth * zoom;
    float displayH = imageHeight * zoom;

    float offsetX = (viewW - displayW) * 0.5f + panX;
    float offsetY = (viewH - displayH) * 0.5f + panY;

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 regionMin = cursorPos;
    const ImVec2 regionMax = ImVec2(cursorPos.x + viewW, cursorPos.y + viewH);

    // Draw background
    ImVec2 bgMin = ImVec2(cursorPos.x + offsetX, cursorPos.y + offsetY);
    ImVec2 bgMax = ImVec2(bgMin.x + displayW, bgMin.y + displayH);

    if (useCheckerboard) {
        const ImVec2 clipMin(std::max(bgMin.x, regionMin.x), std::max(bgMin.y, regionMin.y));
        const ImVec2 clipMax(std::min(bgMax.x, regionMax.x), std::min(bgMax.y, regionMax.y));
        drawCheckerboard(drawList, clipMin, clipMax, 10.0f);
    } else {
        drawList->AddRectFilled(bgMin, bgMax,
            IM_COL32(static_cast<int>(bgColor[0] * 255),
                     static_cast<int>(bgColor[1] * 255),
                     static_cast<int>(bgColor[2] * 255), 255));
    }

    // Draw image
    drawList->AddImage(
        (ImTextureID)texture.srv,
        bgMin, bgMax);

    // Handle input: zoom with wheel, pan with drag
    ImGui::InvisibleButton("##imageview", ImVec2(viewW, viewH));

    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float factor = wheel > 0 ? 1.15f : (1.0f / 1.15f);
            float oldZoom = zoom;
            zoom = std::clamp(zoom * factor, 0.05f, 20.0f);
            fitMode = false;

            // Zoom toward mouse position
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            float relX = mousePos.x - cursorPos.x - viewW * 0.5f - panX;
            float relY = mousePos.y - cursorPos.y - viewH * 0.5f - panY;
            float scale = zoom / oldZoom;
            panX -= relX * (scale - 1.0f);
            panY -= relY * (scale - 1.0f);
        }
    }

    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        panX += delta.x;
        panY += delta.y;
        fitMode = false;
    }
}

void ImageViewer::release() {
    texture.release();
    checkerboard.release();
}
