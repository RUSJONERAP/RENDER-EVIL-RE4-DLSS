#pragma once
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <wrl.h>
#include <d3d12.h>
#include <utility/FunctionHook.hpp>
#include "NativeFsr2Contract.hpp"
#include "NativeComputeStream.hpp"
#include "NativeFsr2BindingsProbe.hpp"

// A non-rendering observer for the RCAS shader identified in the actual RE4 GPU capture.
// This version never calls NGX, changes bindings, skips a dispatch, or edits GPU resources.
namespace hudlab::fsr2gpu {
inline std::unique_ptr<FunctionHook> create_hook,set_hook,dispatch_hook,reset_hook;
inline std::unique_ptr<FunctionHook> device_hook;
inline std::unique_ptr<FunctionHook> library_hook,load_compute_hook;
inline std::unique_ptr<FunctionHook> stream_hook,load_stream_hook;
inline std::mutex library_mutex;
inline std::mutex mutex;
inline std::mutex install_mutex;
struct Pipeline {Microsoft::WRL::ComPtr<ID3D12PipelineState> object;native_sr::Pass pass;};
inline std::unordered_map<ID3D12PipelineState*,Pipeline> recognized;
inline std::unordered_map<ID3D12GraphicsCommandList*,native_sr::Pass> active;
inline std::atomic<uint32_t> dispatches{0};
inline std::atomic<uint32_t> creates{0},loads{0},sets{0},total_dispatches{0};
inline std::atomic<uint32_t> stream_creates{0},stream_loads{0};
inline std::array<std::atomic<uint32_t>,6> pass_counts{};
inline std::atomic<uint32_t> recognized_count{0};
inline bool attempted=false;

inline native_sr::Pass classify(const D3D12_SHADER_BYTECODE& cs) {
    if(!cs.pShaderBytecode) return native_sr::Pass::Unknown;
    return native_sr::identify({(const std::byte*)cs.pShaderBytecode,cs.BytecodeLength});
}

inline void remember(native_sr::Pass pass,HRESULT hr,void** result,const char* source) {
    if(pass==native_sr::Pass::Unknown || FAILED(hr) || !result || !*result) return;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    if(FAILED(((IUnknown*)*result)->QueryInterface(IID_PPV_ARGS(&pso)))) return;
    std::scoped_lock lock{mutex};
    // Retain matched objects: pointer reuse cannot identify an unrelated PSO.
    if(recognized.size()<64) {
        recognized.insert_or_assign(pso.Get(),Pipeline{pso,pass});
        recognized_count.store((uint32_t)recognized.size(),std::memory_order_release);
    }
    spdlog::info("[HUDLab/FSR2-GPU] Captured shader pass={} PSO={:x} source={}",(int)pass,(uintptr_t)pso.Get(),source);
}

inline void mark(ID3D12GraphicsCommandList* list,ID3D12PipelineState* pso) {
    if(recognized_count.load(std::memory_order_acquire)==0) return;
    std::scoped_lock lock{mutex};
    if(auto found=recognized.find(pso);found!=recognized.end()) {
        if(active.contains(list) || active.size()<1024) active.insert_or_assign(list,found->second.pass);
    } else active.erase(list);
}

inline HRESULT STDMETHODCALLTYPE create_compute(ID3D12Device* device,const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,REFIID iid,void** result) {
    const auto number=creates.fetch_add(1);
    if(number<4 && desc) spdlog::info("[HUDLab/FSR2-GPU] Compute creation observed n={} bytes={}",number,desc->CS.BytecodeLength);
    const auto pass=desc?classify(desc->CS):native_sr::Pass::Unknown;
    auto hr=create_hook->get_original<decltype(create_compute)>()(device,desc,iid,result);
    remember(pass,hr,result,"CreateComputePipeline");
    return hr;
}

inline HRESULT STDMETHODCALLTYPE load_compute(ID3D12PipelineLibrary* library,LPCWSTR name,const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,REFIID iid,void** result) {
    const auto number=loads.fetch_add(1);
    const auto pass=desc?classify(desc->CS):native_sr::Pass::Unknown;
    if(number<4 && desc) spdlog::info("[HUDLab/FSR2-GPU] Cached compute load observed n={} bytes={}",number,desc->CS.BytecodeLength);
    const auto hr=load_compute_hook->get_original<decltype(load_compute)>()(library,name,desc,iid,result);
    remember(pass,hr,result,"LoadComputePipeline");
    return hr;
}

inline native_sr::Pass stream_pass(const D3D12_PIPELINE_STATE_STREAM_DESC* desc) {
    if(!desc || !desc->pPipelineStateSubobjectStream) return native_sr::Pass::Unknown;
    const auto shader=native_sr::compute_stream({(const std::byte*)desc->pPipelineStateSubobjectStream,desc->SizeInBytes});
    return shader?classify(*shader):native_sr::Pass::Unknown;
}
inline HRESULT STDMETHODCALLTYPE create_stream(ID3D12Device2* device,const D3D12_PIPELINE_STATE_STREAM_DESC* desc,REFIID iid,void** result) {
    stream_creates.fetch_add(1);
    const auto pass=stream_pass(desc);
    const auto hr=stream_hook->get_original<decltype(create_stream)>()(device,desc,iid,result);
    remember(pass,hr,result,"CreatePipelineState");return hr;
}
inline HRESULT STDMETHODCALLTYPE load_stream(ID3D12PipelineLibrary1* library,LPCWSTR name,const D3D12_PIPELINE_STATE_STREAM_DESC* desc,REFIID iid,void** result) {
    stream_loads.fetch_add(1);
    const auto pass=stream_pass(desc);
    const auto hr=load_stream_hook->get_original<decltype(load_stream)>()(library,name,desc,iid,result);
    remember(pass,hr,result,"LoadPipeline");return hr;
}

inline HRESULT STDMETHODCALLTYPE create_library(ID3D12Device1* device,const void* blob,SIZE_T size,REFIID iid,void** result) {
    const auto hr=library_hook->get_original<decltype(create_library)>()(device,blob,size,iid,result);
    if(SUCCEEDED(hr) && result && *result) {
        Microsoft::WRL::ComPtr<ID3D12PipelineLibrary> library;
        if(SUCCEEDED(((IUnknown*)*result)->QueryInterface(IID_PPV_ARGS(&library)))) {
            std::scoped_lock lock{library_mutex};
            if(!load_compute_hook) {
                const auto table=*(uintptr_t**)library.Get();
                load_compute_hook=std::make_unique<FunctionHook>(table[10],(uintptr_t)&load_compute);
                if(!load_compute_hook->create()) load_compute_hook.reset();
            }
            Microsoft::WRL::ComPtr<ID3D12PipelineLibrary1> library1;
            if(!load_stream_hook && SUCCEEDED(library->QueryInterface(IID_PPV_ARGS(&library1)))) {
                const auto table=*(uintptr_t**)library1.Get();
                load_stream_hook=std::make_unique<FunctionHook>(table[13],(uintptr_t)&load_stream);
                if(!load_stream_hook->create()) load_stream_hook.reset();
            }
        }
    }
    return hr;
}

inline void STDMETHODCALLTYPE set_pipeline(ID3D12GraphicsCommandList* list,ID3D12PipelineState* pso) {
    sets.fetch_add(1);
    mark(list,pso);
    set_hook->get_original<decltype(set_pipeline)>()(list,pso);
}

inline HRESULT STDMETHODCALLTYPE reset(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,ID3D12PipelineState* pso) {
    auto hr=reset_hook->get_original<decltype(reset)>()(list,allocator,pso);
    if(SUCCEEDED(hr)) {mark(list,pso);fsr2bindings::on_reset((uintptr_t)list);}
    return hr;
}

inline void STDMETHODCALLTYPE dispatch(ID3D12GraphicsCommandList* list,UINT x,UINT y,UINT z) {
    total_dispatches.fetch_add(1);
    auto pass=native_sr::Pass::Unknown;
    if(recognized_count.load(std::memory_order_acquire)!=0) {
        std::scoped_lock lock{mutex};if(auto found=active.find(list);found!=active.end()) pass=found->second;
    }
    fsr2bindings::before_dispatch((uintptr_t)list,pass,x,y,z);
    dispatch_hook->get_original<decltype(dispatch)>()(list,x,y,z);
    if(pass!=native_sr::Pass::Unknown) {
        const auto count=pass_counts[(size_t)pass].fetch_add(1);
        if(count<2) spdlog::info("[HUDLab/FSR2-GPU] Native pass={} list={:x} groups={}x{}x{}",(int)pass,(uintptr_t)list,x,y,z);
    }
    if(pass==native_sr::Pass::Rcas) {
        const auto count=dispatches.fetch_add(1);
        if(count<8 || (count<4096 && count%256==0))
            spdlog::info("[HUDLab/FSR2-GPU] RCAS dispatch observed count={} list={:x} groups={}x{}x{} thread={} (original ran unchanged)",
                count,(uintptr_t)list,x,y,z,GetCurrentThreadId());
    }
}

inline void install(ID3D12Device* device) {
    std::scoped_lock setup_lock{install_mutex};
    if(attempted || device==nullptr) return;
    attempted=true;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
    if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))) ||
       FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))) {
        spdlog::info("[HUDLab/FSR2-GPU] Could not obtain command-list method addresses; observer disabled");return;
    }
    list->Close(); // Never submitted. Only used to obtain the same public API method addresses.
    auto dv=*(uintptr_t**)device;
    auto cv=*(uintptr_t**)list.Get();
    // Public ID3D12Device and ID3D12GraphicsCommandList vtable slots, not game offsets.
    create_hook=std::make_unique<FunctionHook>(dv[11],(uintptr_t)&create_compute);
    set_hook=std::make_unique<FunctionHook>(cv[25],(uintptr_t)&set_pipeline);
    reset_hook=std::make_unique<FunctionHook>(cv[10],(uintptr_t)&reset);
    dispatch_hook=std::make_unique<FunctionHook>(cv[14],(uintptr_t)&dispatch);
    const auto create_ok=create_hook->create();
    const auto set_ok=set_hook->create();
    const auto reset_ok=reset_hook->create();
    const auto dispatch_ok=dispatch_hook->create();
    if(!create_ok || !set_ok || !reset_ok || !dispatch_ok) {
        // Keep installed trampolines alive even on partial failure; another thread may
        // already be returning through one. This observer never performs replacement.
        spdlog::info("[HUDLab/FSR2-GPU] Hook setup incomplete; pass-through trampolines retained");return;
    }
    fsr2bindings::pipeline_changed=&mark;
    fsr2bindings::install(device,dv,cv);
    Microsoft::WRL::ComPtr<ID3D12Device1> device1;
    if(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device1)))) {
        auto table=*(uintptr_t**)device1.Get();
        library_hook=std::make_unique<FunctionHook>(table[44],(uintptr_t)&create_library);
        if(!library_hook->create()) library_hook.reset();
    }
    Microsoft::WRL::ComPtr<ID3D12Device2> device2;
    if(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device2)))) {
        const auto table=*(uintptr_t**)device2.Get();
        stream_hook=std::make_unique<FunctionHook>(table[47],(uintptr_t)&create_stream);
        if(!stream_hook->create()) stream_hook.reset();
    }
    spdlog::info("[HUDLab/FSR2-GPU] Bytecode-specific GPU observer installed; no render replacement");
}

inline HRESULT WINAPI create_device(IUnknown* adapter,D3D_FEATURE_LEVEL level,REFIID iid,void** out) {
    const auto hr=device_hook->get_original<decltype(create_device)>()(adapter,level,iid,out);
    if(SUCCEEDED(hr) && out && *out) {
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if(SUCCEEDED(((IUnknown*)*out)->QueryInterface(IID_PPV_ARGS(&device)))) install(device.Get());
    }
    return hr;
}

inline void install_early() {
#ifdef RE4
    // Respect the existing diagnostic opt-in even before the normal mod config loads.
    // No permanent D3D12 observer overhead in regular builds with HUDTrace disabled.
    wchar_t exe[MAX_PATH]{};
    const auto length=GetModuleFileNameW(nullptr,exe,MAX_PATH);
    if(!length || length>=MAX_PATH) return;
    auto config_path=std::filesystem::path(exe).replace_filename(L"re2_fw_config.txt");
    std::ifstream config(config_path);
    bool enabled=false;
    for(std::string line;std::getline(config,line);) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(line=="TemporalUpscaler_HUDTrace=true") {enabled=true;break;}
    }
    if(!enabled) return;
    const auto base=(uint8_t*)GetModuleHandleW(nullptr);
    const auto dos=(IMAGE_DOS_HEADER*)base;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return;
    const auto nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp!=0x69a8f652 ||
       nt->OptionalHeader.SizeOfImage!=0x0e405000) return;
    // Startup thread, after logger creation, outside DllMain/loader lock. No driver settings.
    const auto d3d12=LoadLibraryExW(L"d3d12.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!d3d12) return;
    const auto proc=GetProcAddress(d3d12,"D3D12CreateDevice");
    if(!proc) return;
    device_hook=std::make_unique<FunctionHook>((uintptr_t)proc,(uintptr_t)&create_device);
    if(device_hook->create()) spdlog::info("[HUDLab/FSR2-GPU] Early device observer armed before VM/render initialization");
    else device_hook.reset();
#endif
}

inline void log_stats() {
    spdlog::info("[HUDLab/FSR2-GPU] Counters create={} cached={} stream={} cachedStream={} set={} dispatch={} matched={}",
        creates.load(),loads.load(),stream_creates.load(),stream_loads.load(),sets.load(),total_dispatches.load(),dispatches.load());
    fsr2bindings::log_stats();
}
}
