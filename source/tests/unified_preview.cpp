// Renders the actual ImGui panel offscreen with D3D11 WARP, without loading mods or opening a window.
#include "UnifiedPanel.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include "imgui_impl_dx11.h"
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void require(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D11 WARP failure "+std::to_string((unsigned)hr));}
int main(){try {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    require(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
    D3D11_TEXTURE2D_DESC desc{};desc.Width=1120;desc.Height=840;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target,staging;ComPtr<ID3D11RenderTargetView> view;
    require(device->CreateTexture2D(&desc,nullptr,&target));require(device->CreateRenderTargetView(target.Get(),nullptr,&view));
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    require(device->CreateTexture2D(&desc,nullptr,&staging));
    IMGUI_CHECKVERSION();ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize={1120,840};io.DeltaTime=1.0f/60;
    re4unified::panel_font=io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",18);
    if(!re4unified::panel_font)throw std::runtime_error("Segoe UI font unavailable");
    if(!ImGui_ImplDX11_Init(device.Get(),context.Get()))throw std::runtime_error("ImGui renderer init failed");
    for(int lang=0;lang<2;++lang)for(int tab=0;tab<6;++tab) {
        for(int frame=0;frame<3;++frame) {
            ImGui_ImplDX11_NewFrame();ImGui::NewFrame();auto& panel=re4unified::panel;
            panel.initialized=true;panel.refreshed=GetTickCount64();panel.russian=lang==0;panel.tab=tab;
            panel.actual_status="OFFLINE PREVIEW: no game or mod loaded";panel.nr_running=false;panel.live_fresh=false;
            ImGui::SetNextWindowSize({1020,740},ImGuiCond_Always);ImGui::SetNextWindowPos({50,50},ImGuiCond_Always);
            re4unified::draw();ImGui::Render();
            const float clear[]{0.025f,0.03f,0.04f,1};context->ClearRenderTargetView(view.Get(),clear);
            auto raw_view=view.Get();context->OMSetRenderTargets(1,&raw_view,nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        require(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        std::ofstream out("tests/build/preview_"+std::string(lang==0?"ru":"en")+"_"+std::to_string(tab)+".rgba",std::ios::binary);
        for(UINT y=0;y<desc.Height;++y)out.write((const char*)mapped.pData+y*mapped.RowPitch,desc.Width*4);
        context->Unmap(staging.Get(),0);if(!out)throw std::runtime_error("Preview write failed");
    }
    ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();std::cout<<"Rendered 12 actual-panel offscreen previews; no game DLL loaded.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
