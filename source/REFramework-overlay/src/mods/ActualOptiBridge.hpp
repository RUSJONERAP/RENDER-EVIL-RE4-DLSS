#pragma once
#include "UnifiedRuntime.hpp"
#include "UnifiedGpuLifetime.hpp"
#include "UnifiedNrApi.hpp"
#include "UnifiedNgxView.hpp"
#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <cstring>
#include <string_view>

// Source-level compatibility adapter for the unmodified Actual DLSS plugin.
// Only its named LoadLibraryExW import is redirected, only for the NGX driver-core
// filename. No on-disk DLL patch, extra proxy, or global loader detour.
namespace re4unified::interop {
using Load=HMODULE(WINAPI*)(LPCWSTR,HANDLE,DWORD);
using Proc=FARPROC(WINAPI*)(HMODULE,LPCSTR);
inline Load original=nullptr;
inline Proc original_proc=nullptr;
using Create=uint32_t(*)(ID3D12GraphicsCommandList*,int32_t,void*,void**);
using Evaluate=uint32_t(*)(ID3D12GraphicsCommandList*,const void*,void*,void*);
inline Create create_provider=nullptr;
inline Evaluate evaluate_provider=nullptr;
using Shutdown=uint32_t(*)(ID3D12Device*);
inline Shutdown shutdown_provider=nullptr;
inline HMODULE provider=nullptr;
inline std::atomic<uint32_t> redirects{0};
inline bool attached=false;
inline std::string status="not attached";
inline std::unordered_map<uint32_t,Extent> feature_views;
inline std::atomic<uint32_t> primary_width{0},primary_height{0},auxiliary_skips{0};

inline bool ngx_core_name(std::wstring_view value) {
    const auto slash=value.find_last_of(L"\\/");if(slash!=value.npos)value.remove_prefix(slash+1);
    std::wstring name(value);for(auto& c:name)c=(wchar_t)towlower(c);
    return name==L"_nvngx.dll" || name==L"nvngx.dll";
}
inline HMODULE WINAPI load(LPCWSTR name,HANDLE file,DWORD flags) {
    if(name && provider && ngx_core_name(name)) {
        HMODULE retained=nullptr;
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)provider,&retained)) {
            redirects.fetch_add(1);
            OutputDebugStringA("[RE:NDER EVIL] Actual DLSS NGX request routed to existing OptiScaler\n");
            return retained; // Matching reference for the caller's eventual FreeLibrary.
        }
    }
    return original?original(name,file,flags):nullptr;
}
inline void** find_loader_slot(HMODULE module,const char* imported="LoadLibraryExW") {
    auto base=(std::byte*)module;auto dos=(IMAGE_DOS_HEADER*)base;
    if(!base || dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>1024*1024)return nullptr;
    auto nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return nullptr;
    const auto size=nt->OptionalHeader.SizeOfImage;
    const auto fits=[&](uint64_t rva,uint64_t n){return rva<size && n<=size-rva;};
    const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!fits(directory.VirtualAddress,directory.Size))return nullptr;
    void** result=nullptr;
    for(size_t i=0;i<directory.Size/sizeof(IMAGE_IMPORT_DESCRIPTOR);++i) {
        auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+directory.VirtualAddress+i*sizeof(IMAGE_IMPORT_DESCRIPTOR));
        if(!desc->Name)break;
        if(!fits(desc->Name,1))return nullptr;
        const auto library=(const char*)(base+desc->Name);
        if(!std::memchr(library,0,(size_t)std::min<uint64_t>(size-desc->Name,128)))return nullptr;
        if(_stricmp(library,"kernel32.dll") && _stricmp(library,"kernelbase.dll"))continue;
        if(!desc->OriginalFirstThunk || !desc->FirstThunk)continue;
        for(size_t n=0;n<4096;++n) {
            const auto names=(uint64_t)desc->OriginalFirstThunk+n*sizeof(IMAGE_THUNK_DATA64);
            const auto values=(uint64_t)desc->FirstThunk+n*sizeof(IMAGE_THUNK_DATA64);
            if(!fits(names,sizeof(IMAGE_THUNK_DATA64)) || !fits(values,sizeof(void*)))return nullptr;
            const auto name=((IMAGE_THUNK_DATA64*)(base+names))->u1.AddressOfData;if(!name)break;
            if(IMAGE_SNAP_BY_ORDINAL64(name))continue;
            if(!fits(name,sizeof(IMAGE_IMPORT_BY_NAME)))return nullptr;
            const auto text=(const char*)(base+name+sizeof(WORD));
            const auto available=std::min<uint64_t>(size-name-sizeof(WORD),128);
            if(!std::memchr(text,0,(size_t)available))return nullptr;
            if(std::strcmp(text,imported)==0) {if(result)return nullptr;result=(void**)(base+values);}
        }
    }
    return result;
}
inline uint32_t create_feature(ID3D12GraphicsCommandList* list,int32_t feature,void* params,void** handle) {
    std::lock_guard lock(lifetime::api_mutex);
    const auto result=create_provider(list,feature,params,handle);
    if(result==1 && params && handle && *handle) {
        auto* view=(NgxParametersView*)params;Extent extent;
        if(view->read("OutWidth",&extent.width)==1 && view->read("OutHeight",&extent.height)==1) {
            uint32_t id=0;std::memcpy(&id,*handle,sizeof(id));feature_views[id]=extent;
        }
    }
    return result;
}
inline uint32_t evaluate_feature(ID3D12GraphicsCommandList* list,const void* handle,void* params,void* callback) {
    std::lock_guard lock(lifetime::api_mutex);
    uint32_t id=0;if(handle)std::memcpy(&id,handle,sizeof(id));
    const auto found=feature_views.find(id);
    const bool auxiliary=found!=feature_views.end() &&
        !nr_primary_view(found->second,{primary_width.load(),primary_height.load()});
    // F5 owns one NR model. Running it on a 1024-square cutscene view as well
    // as the display-sized view repeatedly rebuilds it. SR still runs on both.
    struct RestoreNr {
        bool restore=false;
        ~RestoreNr(){if(restore)nr_api::api.set_bool("Enabled",1);}
    } restore;
    if(auxiliary && nr_api::api.connect(provider)) {
        int32_t enabled=0;
        if(nr_api::api.get_bool("Enabled",&enabled)==1 && enabled && nr_api::api.set_bool("Enabled",0)==1) {
            restore.restore=true;
            const auto count=auxiliary_skips.fetch_add(1);
            if(count==0)lifetime::trace("NR auxiliary view bypass active; SR remains enabled on the auxiliary view");
        }
    }
    return evaluate_provider(list,handle,params,callback);
}
inline uint32_t shutdown_device(ID3D12Device* device) {
    std::lock_guard lock(lifetime::api_mutex);
    lifetime::stop();return shutdown_provider(device);
}
inline FARPROC WINAPI procedure(HMODULE module,LPCSTR name) {
    const auto result=original_proc?original_proc(module,name):nullptr;
    if(module==provider && (uintptr_t)name>0xffff && result) {
        if(std::strcmp(name,"NVSDK_NGX_D3D12_ReleaseFeature")==0) {
            lifetime::release_provider=(lifetime::Release)result;return (FARPROC)&lifetime::release;
        }
        if(std::strcmp(name,"NVSDK_NGX_D3D12_CreateFeature")==0) {
            create_provider=(Create)result;return (FARPROC)&create_feature;
        }
        if(std::strcmp(name,"NVSDK_NGX_D3D12_EvaluateFeature")==0) {
            evaluate_provider=(Evaluate)result;return (FARPROC)&evaluate_feature;
        }
        if(std::strcmp(name,"NVSDK_NGX_D3D12_Shutdown1")==0) {
            shutdown_provider=(Shutdown)result;return (FARPROC)&shutdown_device;
        }
    }
    return result;
}
inline bool replace_import(void** slot,void* expected,void* replacement) {
    DWORD before=0;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&before))return false;
    const auto previous=InterlockedCompareExchangePointer(slot,replacement,expected);
    DWORD ignored=0;VirtualProtect(slot,sizeof(void*),before,&ignored);
    return previous==expected;
}
inline bool attach(HMODULE plugin) {
    if(!active())return false;
    if(attached)return true;
    provider=GetModuleHandleW(L"version.dll");
    wchar_t path[32768]{};
    if(!provider || !GetModuleFileNameW(provider,path,32768) ||
       _wcsicmp(std::filesystem::path(path).parent_path().c_str(),root().c_str())!=0) {
        status="OptiScaler game proxy not loaded";provider=nullptr;return false;
    }
    for(const auto name:{"NVSDK_NGX_D3D12_Init","NVSDK_NGX_D3D12_CreateFeature","NVSDK_NGX_D3D12_EvaluateFeature","NVSDK_NGX_D3D12_GetCapabilityParameters"})
        if(!GetProcAddress(provider,name)){status="NGX exports missing from provider";provider=nullptr;return false;}
    const auto slot=find_loader_slot(plugin),proc_slot=find_loader_slot(plugin,"GetProcAddress");
    if(!slot || !proc_slot){status="Actual DLSS loader imports are absent/ambiguous";return false;}
    original=(Load)*slot;
    original_proc=(Proc)*proc_slot;
    if(!replace_import(proc_slot,(void*)original_proc,(void*)&procedure)){status="Procedure import patch refused";return false;}
    attached=replace_import(slot,(void*)original,(void*)&load);
    if(!attached)replace_import(proc_slot,(void*)&procedure,(void*)original_proc);
    status=attached?"NGX bridge + GPU-fenced SR retirement attached":"Loader changed during attachment";
    return attached;
}
}
