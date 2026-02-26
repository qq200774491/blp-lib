#include "app.h"

#include <dxgi.h>

bool CreateDeviceD3D(HWND hWnd, DX11State* state) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &state->swapChain, &state->device, &featureLevel, &state->deviceContext);

    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &state->swapChain, &state->device, &featureLevel, &state->deviceContext);
    }

    if (FAILED(hr)) return false;

    CreateRenderTarget(state);
    return true;
}

void CleanupDeviceD3D(DX11State* state) {
    CleanupRenderTarget(state);
    if (state->swapChain) { state->swapChain->Release(); state->swapChain = nullptr; }
    if (state->deviceContext) { state->deviceContext->Release(); state->deviceContext = nullptr; }
    if (state->device) { state->device->Release(); state->device = nullptr; }
}

void CreateRenderTarget(DX11State* state) {
    ID3D11Texture2D* pBackBuffer = nullptr;
    state->swapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        state->device->CreateRenderTargetView(pBackBuffer, nullptr, &state->renderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget(DX11State* state) {
    if (state->renderTargetView) {
        state->renderTargetView->Release();
        state->renderTargetView = nullptr;
    }
}
