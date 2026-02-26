#pragma once

#include <d3d11.h>

struct DX11State {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* deviceContext = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;
};

bool CreateDeviceD3D(HWND hWnd, DX11State* state);
void CleanupDeviceD3D(DX11State* state);
void CreateRenderTarget(DX11State* state);
void CleanupRenderTarget(DX11State* state);
