#include "UnifiedGpuLifetime.hpp"
#include <dxgi1_6.h>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static uint32_t freed=0;
static void* expected_handle=nullptr;
static uint32_t fake_release(void* handle){require(handle==expected_handle,"Original opaque handle identity preserved");freed=*(uint32_t*)handle;return 1;}
int main(){try {
    using namespace re4unified::lifetime;
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    require(SUCCEEDED(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory))),"DXGI factory");
    require(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))),"WARP adapter");
    require(SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))),"D3D12 WARP device");
    ComPtr<ID3D12CommandQueue> game_queue;D3D12_COMMAND_QUEUE_DESC desc{};
    require(SUCCEEDED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&game_queue))),"Queue");
    ComPtr<ID3D12Fence> blocker;
    require(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&blocker))),"Blocker fence");
    require(SUCCEEDED(game_queue->Wait(blocker.Get(),1)),"Block queue to simulate in-flight GPU work");
    release_provider=&fake_release;uint32_t handle=751;
    expected_handle=&handle;
    require(release(&handle)==1 && pending.load()==1 && freed==0,"Release is deferred");
    for(int i=0;i<64;++i)after_present(game_queue.Get());
    require(freed==0 && pending.load()==1,"Even 64 presents cannot release before the actual GPU fence");
    require(SUCCEEDED(blocker->Signal(1)),"Allow the GPU queue to proceed");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"Fence event");
    require(SUCCEEDED(fence->SetEventOnCompletion(next_fence,event)),"Fence completion notification");
    const auto wait=WaitForSingleObject(event,3000);CloseHandle(event);require(wait==WAIT_OBJECT_0,"GPU fence completed");
    after_present(game_queue.Get());require(freed==751 && pending.load()==0 && released.load()==1,"Exact handle released after completion");
    after_present(game_queue.Get());require(released.load()==1,"No duplicate release");
    uint32_t dying=752;release(&dying);{std::lock_guard lock(api_mutex);stop();}
    after_present(game_queue.Get());require(pending.load()==0 && released.load()==1,"Shutdown cannot call back into destroyed provider contexts");
    std::cout<<"PASS real D3D12 WARP: held through 64 blocked presents, released exactly once after GPU completion; no game DLL loaded\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
