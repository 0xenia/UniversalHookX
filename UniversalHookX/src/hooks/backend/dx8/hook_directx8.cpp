#include "../../../backend.hpp"
#include "../../../console/console.hpp"

#ifdef ENABLE_BACKEND_DX8
#include <Windows.h>
#include <memory>
#include "../../../dependencies/DirectX8/Include/d3d8.h"
#include "hook_directx8.hpp"
#include "../../../dependencies/imgui/imgui_impl_dx8.h"
#include "../../../dependencies/imgui/imgui_impl_win32.h"
#include "../../../dependencies/minhook/MinHook.h"
#include "../../../menu/menu.hpp"
#include "../../hooks.hpp"

#pragma comment(lib, "d3d8.lib")

namespace {
    LPDIRECT3D8 g_pD3D = nullptr;
    LPDIRECT3DDEVICE8 g_pd3dDevice = nullptr;
    bool g_initialized = false;
    HWND g_gameWindow = nullptr;

    using Reset_t = HRESULT(WINAPI*)(IDirect3DDevice8*, D3DPRESENT_PARAMETERS*);
    using Present_t = HRESULT(WINAPI*)(IDirect3DDevice8*, const RECT*, const RECT*, HWND, const RGNDATA*);

    Reset_t oReset = nullptr;
    Present_t oPresent = nullptr;

    void RenderImGui_DX8(IDirect3DDevice8* pDevice) {
        if (!H::bShuttingDown && ImGui::GetCurrentContext()) {
            ImGui_ImplDX8_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            Menu::Render();

            ImGui::EndFrame();
            if (pDevice->BeginScene() == D3D_OK) {
                ImGui::Render();
                ImGui_ImplDX8_RenderDrawData(ImGui::GetDrawData());
                pDevice->EndScene();
            }
        }
    }

    void CleanupDeviceD3D8() {
        if (g_pD3D) {
            g_pD3D->Release();
            g_pD3D = nullptr;
        }
        if (g_pd3dDevice) {
            g_pd3dDevice->Release();
            g_pd3dDevice = nullptr;
        }
    }

    bool CreateDeviceD3D8(HWND hWnd) {
        auto d3d8 = GetModuleHandleW(L"d3d8.dll");
        if (!d3d8)
            d3d8 = LoadLibraryW(L"d3d8.dll");
        if (!d3d8) {
            LOG("[!] Failed to load d3d8.dll\n");
            return false;
        }

        auto Direct3DCreate8_fn = (decltype(&Direct3DCreate8))GetProcAddress(d3d8, "Direct3DCreate8");
        if (!Direct3DCreate8_fn) {
            LOG("[!] Failed to get Direct3DCreate8\n");
            return false;
        }

        if ((g_pD3D = Direct3DCreate8_fn(D3D_SDK_VERSION)) == nullptr) {
            LOG("[!] Direct3DCreate8 failed\n");
            return false;
        }

        D3DPRESENT_PARAMETERS d3dpp = {};
        d3dpp.BackBufferWidth = 0;
        d3dpp.BackBufferHeight = 0;
        d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
        d3dpp.BackBufferCount = 1;
        d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        d3dpp.Windowed = TRUE;
        d3dpp.EnableAutoDepthStencil = TRUE;
        d3dpp.AutoDepthStencilFormat = D3DFMT_D16;

        HRESULT hr = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &g_pd3dDevice);

        if (FAILED(hr)) {
            LOG("[!] CreateDevice() failed. [rv: %lu]\n", hr);
            return false;
        }

        return true;
    }

    HRESULT WINAPI hkReset(IDirect3DDevice8* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
        ImGui_ImplDX8_InvalidateDeviceObjects();
        auto hr = oReset(pDevice, pPresentationParameters);
        
        if (SUCCEEDED(hr)) {
            ImGui_ImplDX8_CreateDeviceObjects();
        }
        return hr;
    }

    HRESULT WINAPI hkPresent(IDirect3DDevice8* pDevice,
        const RECT* pSourceRect,
        const RECT* pDestRect,
        HWND hDestWindowOverride,
        const RGNDATA* pDirtyRegion) {
        if (!g_initialized) {
            g_initialized = true;
            HWND targetWnd = hDestWindowOverride ? hDestWindowOverride : g_gameWindow;
            Menu::InitializeContext(targetWnd);
            ImGui_ImplWin32_Init(targetWnd);
            ImGui_ImplDX8_Init(pDevice);
            LOG("[+] ImGui DirectX8 initialized\n");
        }
        
        RenderImGui_DX8(pDevice);
        return oPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    }
}

namespace DX8 {
    void Hook(HWND hwnd) {
        g_gameWindow = hwnd;

        if (!CreateDeviceD3D8(hwnd)) {
            return;
        }

        if (g_pd3dDevice) {
            void** pVTable = *reinterpret_cast<void***>(g_pd3dDevice);
            oReset = reinterpret_cast<Reset_t>(pVTable[14]);
            oPresent = reinterpret_cast<Present_t>(pVTable[15]);

            if (MH_CreateHook(oReset, &hkReset, reinterpret_cast<void**>(&oReset)) != MH_OK ||
                MH_CreateHook(oPresent, &hkPresent, reinterpret_cast<void**>(&oPresent)) != MH_OK) {
                LOG("[!] Failed to create hooks\n");
                return;
            }

            MH_EnableHook(MH_ALL_HOOKS);
            LOG("[+] Hooks installed successfully\n");
            CleanupDeviceD3D8();
        }
    }

    void Unhook() {
        MH_DisableHook(MH_ALL_HOOKS);
        
        if (ImGui::GetCurrentContext()) {
            if (ImGui::GetIO().BackendRendererUserData)
                ImGui_ImplDX8_Shutdown();
            if (ImGui::GetIO().BackendPlatformUserData)
                ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
    }
}
#else
namespace DX8 {
    void Hook(HWND) { LOG("[!] DirectX8 backend is not enabled!\n"); }
    void Unhook() { }
}
#endif
