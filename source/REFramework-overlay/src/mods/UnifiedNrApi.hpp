#pragma once
#include "UnifiedRuntime.hpp"
#include <cstdint>

// Consumer of OptiScaler's documented DlssNr_Api.h ABI v1 (mrcgibb9876-hash).
// Resolve from the existing provider; never load a second OptiScaler instance.
namespace re4unified::nr_api {
struct Status {
    uint32_t structSize=sizeof(Status);
    int32_t running=0,runningVulkan=0,enabled=0;
    double gpuMs=-1;
    uint64_t framesVulkan=0;
    const char* failureReason=nullptr;
};
static_assert(sizeof(Status)==40);
struct Api {
    using Version=int32_t(*)();
    using ReadStatus=int32_t(*)(Status*);
    using ReadFloat=int32_t(*)(const char*,float*);
    using WriteFloat=int32_t(*)(const char*,float);
    using ReadInt=int32_t(*)(const char*,int32_t*);
    using WriteInt=int32_t(*)(const char*,int32_t);
    Version version=nullptr,save=nullptr;
    ReadStatus status=nullptr;
    ReadFloat get_float=nullptr;
    WriteFloat set_float=nullptr;
    ReadInt get_int=nullptr,get_bool=nullptr;
    WriteInt set_int=nullptr,set_bool=nullptr;
    HMODULE module=nullptr;
    bool ready=false;
    bool connect(HMODULE candidate) {
        if(candidate==module && ready)return true;
        if(!candidate)return false;
        module=candidate;
        version=(Version)GetProcAddress(module,"OptiNr_AbiVersion");
        // Never call any other entry point on an unknown ABI.
        if(!version || version()!=1)return ready=false;
        status=(ReadStatus)GetProcAddress(module,"OptiNr_GetStatus");
        save=(Version)GetProcAddress(module,"OptiNr_Save");
        get_float=(ReadFloat)GetProcAddress(module,"OptiNr_GetFloat");
        set_float=(WriteFloat)GetProcAddress(module,"OptiNr_SetFloat");
        get_int=(ReadInt)GetProcAddress(module,"OptiNr_GetInt");
        set_int=(WriteInt)GetProcAddress(module,"OptiNr_SetInt");
        get_bool=(ReadInt)GetProcAddress(module,"OptiNr_GetBool");
        set_bool=(WriteInt)GetProcAddress(module,"OptiNr_SetBool");
        return ready=status && save && get_float && set_float && get_int && set_int && get_bool && set_bool;
    }
};
inline Api api;
}
