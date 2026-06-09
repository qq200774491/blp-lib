#pragma once

#include <cstdint>
#include <d3d11.h>

struct ImageTexture {
    ID3D11ShaderResourceView* srv = nullptr;
    int width = 0;
    int height = 0;

    bool createFromRGBA(ID3D11Device* device, const uint8_t* rgba, int w, int h);
    void release();
    bool isValid() const;

    static ImageTexture createCheckerboard(ID3D11Device* device, int tileSize = 16);
};
