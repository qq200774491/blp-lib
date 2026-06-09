#pragma once

#include "image_texture.h"
#include "imgui.h"

struct ImageViewer {
    ImageTexture texture;
    ImageTexture checkerboard;
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    bool fitMode = true;
    bool hasImage = false;
    int imageWidth = 0;
    int imageHeight = 0;
    bool useCheckerboard = true;
    float bgColor[3] = {0.878f, 0.894f, 0.922f}; // #e0e4eb

    void init(ID3D11Device* device);
    void setImage(ID3D11Device* device, const uint8_t* rgba, int w, int h);
    void clearImage();
    void fitToView(float viewW, float viewH);
    void setZoom(float value, float pivotX = 0.5f, float pivotY = 0.5f);
    void render(float viewW, float viewH);
    void release();
};
