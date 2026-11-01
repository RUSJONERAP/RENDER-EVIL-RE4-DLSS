#pragma once
#include "UnifiedRuntime.hpp"
#include "UnifiedRetirement.hpp"
#include <d3d12.h>
#include <wrl/client.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace re4unified::lifetime {
using Release=uint32_t(*)(void*);
inline Release release_provider=nullptr;
// F5's context map is not concurrent: retirement and this caller's NGX
// Create/Evaluate operations must share the same gate.
inline std::recursive_mutex api_mutex;
inline Retirement retired;
inline std::unordered_map<uint32_t,void*> handles;
inline std::atomic<bool> stopped=false;
inline std::mutex mutex;
inline Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
inline Microsoft::WRL::ComPtr<ID3D12Fence> fence;
inline uint64_t next_fence=0;
inline std::atomic<uint32_t> deferred{0},released{0},pending{0};
inline void trace(const char* message) {
    if(!active())return;
    try {
        const auto path=root()/L"reframework/data/re4unified"/(L"gpu_lifetime_"+std::to_wstring(GetCurrentProcessId())+L".log");
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path,std::ios::app);out<<GetTickCount64()<<" "<<message<<"\n";
    }catch(...){}
}
inline uint32_t release(void* handle) {
    if(!handle)return 1; // NVSDK_NGX_Result_Success.
    std::lock_guard api_lock(api_mutex);
    if(stopped.load())return 1;
    uint32_t id=0;std::memcpy(&id,handle,sizeof(id));
    std::lock_guard lock(mutex);
    if(retired.defer(id)){handles.emplace(id,handle);deferred.fetch_add(1);}
    pending.store((uint32_t)retired.size());
    char message[160]{};sprintf_s(message,"defer id=%u handle=%p pending=%u",id,handle,pending.load());trace(message);
    // Keep the provider's context alive; Actual DLSS no longer owns the handle.
    return 1;
}
inline void after_present(ID3D12CommandQueue* submitted_queue) {
    if(!release_provider || !submitted_queue || !pending.load() || stopped.load())return;
    std::vector<std::pair<uint32_t,void*>> ready;
    {
        std::lock_guard lock(mutex);
        if(!queue) {
            if(submitted_queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return;
            Microsoft::WRL::ComPtr<ID3D12Device> device;
            if(FAILED(submitted_queue->GetDevice(IID_PPV_ARGS(&device))))return;
            if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return;
            queue=submitted_queue;
            char message[160]{};sprintf_s(message,"GPU lifetime fence created on DIRECT queue=%p",(void*)submitted_queue);trace(message);
        }
        // A fence from another queue cannot prove this device's work has retired.
        if(queue.Get()!=submitted_queue)return;
        const auto snapshot=retired.unsignalled();
        if(!snapshot.empty()) {
            const auto value=++next_fence;
            if(SUCCEEDED(queue->Signal(fence.Get(),value))) {
                retired.submitted(snapshot,value);
                char message[160]{};sprintf_s(message,"post-Present signal=%llu covers %zu retired handle(s)",value,snapshot.size());trace(message);
            }
        }
        for(auto id:retired.completed(fence->GetCompletedValue())) {
            const auto found=handles.find(id);
            if(found!=handles.end()){ready.emplace_back(id,found->second);handles.erase(found);}
        }
        pending.store((uint32_t)retired.size());
    }
    for(auto [id,handle]:ready) {
        std::lock_guard api_lock(api_mutex);
        if(stopped.load())break;
        const auto result=release_provider(handle);
        if(result==1)released.fetch_add(1);
        char line[180]{};sprintf_s(line,"[RE:NDER EVIL] fence-completed SR release id=%u result=0x%08X\n",id,result);
        OutputDebugStringA(line);
        trace(line);
    }
}
inline void stop() {
    // Call with api_mutex held, before the provider's shutdown can destroy contexts.
    stopped.store(true);
    trace("provider shutdown: stop retirement callbacks before contexts are destroyed");
    std::lock_guard lock(mutex);retired=Retirement{};handles.clear();pending.store(0);
}
}
