#include <Windows.h>
#include <d3d10.h>

#include <memory>
#include <mutex>

#include "hook_directx10.hpp"

#include "../../../dependencies/imgui/imgui_impl_dx10.h"
#include "../../../dependencies/imgui/imgui_impl_win32.h"
#include "../../../dependencies/minhook/MinHook.h"

#include "../../../console/console.hpp"
#include "../../hooks.hpp"

static ID3D10Device*            g_pd3dDevice = NULL;
static ID3D10RenderTargetView*  g_pd3dRenderTarget = NULL;
static IDXGISwapChain*          g_pSwapChain = NULL;

static void CleanupDeviceD3D10( );
static void CleanupRenderTarget( );

static bool CreateDeviceD3D10(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};

    // Create the D3DDevice
    ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hWnd;
    swapChainDesc.SampleDesc.Count = 1;

    HRESULT result = D3D10CreateDeviceAndSwapChain(NULL, D3D10_DRIVER_TYPE_NULL, NULL, 0, D3D10_SDK_VERSION, &swapChainDesc, &g_pSwapChain, &g_pd3dDevice);
    while (result != S_OK) {
        LOG("[!] D3D10CreateDeviceAndSwapChain() failed. [rv: %lu]\n", result);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        result = D3D10CreateDeviceAndSwapChain(NULL, D3D10_DRIVER_TYPE_NULL, NULL, 0, D3D10_SDK_VERSION, &swapChainDesc, &g_pSwapChain, &g_pd3dDevice);
    }

    return true;
}

static void CreateRenderTarget(IDXGISwapChain* pSwapChain) {
    ID3D10Texture2D* pBackBuffer;
    pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        D3D10_RENDER_TARGET_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.ViewDimension = D3D10_RTV_DIMENSION_TEXTURE2D;

        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, &desc, &g_pd3dRenderTarget);
        pBackBuffer->Release( );
    }
}

static std::add_pointer_t<HRESULT WINAPI(IDXGISwapChain*, UINT, UINT)> oPresent;
static HRESULT WINAPI hkPresent(IDXGISwapChain* pSwapChain,
                                UINT SyncInterval,
                                UINT Flags) {
    static std::once_flag once;
    std::call_once(once, [ pSwapChain ]( ) {
        if (SUCCEEDED(pSwapChain->GetDevice(IID_PPV_ARGS(&g_pd3dDevice)))) {
            ImGui_ImplDX10_Init(g_pd3dDevice);
        }
    });

    if (!g_pd3dRenderTarget) {
        CreateRenderTarget(pSwapChain);
    }

    if (ImGui::GetCurrentContext( ) && g_pd3dRenderTarget) {
        ImGui_ImplDX10_NewFrame( );
        ImGui_ImplWin32_NewFrame( );
        ImGui::NewFrame( );

        if (H::bShowDemoWindow) {
            ImGui::ShowDemoWindow( );
        }

        ImGui::Render( );

        g_pd3dDevice->OMSetRenderTargets(1, &g_pd3dRenderTarget, NULL);
        ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData( ));
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

static std::add_pointer_t<HRESULT WINAPI(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT)> oResizeBuffers;
static HRESULT WINAPI hkResizeBuffers(IDXGISwapChain* pSwapChain,
                                      UINT        BufferCount,
                                      UINT        Width,
                                      UINT        Height,
                                      DXGI_FORMAT NewFormat,
                                      UINT        SwapChainFlags) {
    CleanupRenderTarget( );

    return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

namespace DX10 {
    void Hook(HWND hwnd) {
        if (!CreateDeviceD3D10(GetConsoleWindow( ))) {
            return;
        }

        LOG("[+] DirectX10: g_pd3dDevice: 0x%p\n", g_pd3dDevice);
        LOG("[+] DirectX10: g_pSwapChain: 0x%p\n", g_pSwapChain);

        if (g_pd3dDevice) {
            // Init ImGui
            ImGui::CreateContext( );
            ImGui_ImplWin32_Init(hwnd);

            ImGuiIO& io = ImGui::GetIO( );

            io.IniFilename = nullptr;
            io.LogFilename = nullptr;

            // Hook
            void** pVTable = *reinterpret_cast<void***>(g_pSwapChain);

            void* fnPresent = pVTable[8];
            void* fnResizeBuffer = pVTable[13];

            CleanupDeviceD3D10( );

            static MH_STATUS presentStatus = MH_CreateHook(reinterpret_cast<void**>(fnPresent), &hkPresent, reinterpret_cast<void**>(&oPresent));
            static MH_STATUS resizeStatus = MH_CreateHook(reinterpret_cast<void**>(fnResizeBuffer), &hkResizeBuffers, reinterpret_cast<void**>(&oResizeBuffers));

            MH_EnableHook(fnPresent);
            MH_EnableHook(fnResizeBuffer);
        }
    }

    void Unhook( ) {
        if (ImGui::GetCurrentContext( )) {
            if (ImGui::GetIO( ).BackendRendererUserData)
                ImGui_ImplDX10_Shutdown( );

            ImGui_ImplWin32_Shutdown( );
            ImGui::DestroyContext( );
        }

        CleanupDeviceD3D10( );
    }
}

static void CleanupRenderTarget( ) {
    if (g_pd3dRenderTarget) { g_pd3dRenderTarget->Release( ); g_pd3dRenderTarget = NULL; }
}

static void CleanupDeviceD3D10( ) {
    CleanupRenderTarget( );

    if (g_pd3dDevice) { g_pd3dDevice->Release( ); g_pd3dDevice = NULL; }
    if (g_pd3dRenderTarget) { g_pd3dRenderTarget->Release( ); g_pd3dRenderTarget = NULL; }
}
