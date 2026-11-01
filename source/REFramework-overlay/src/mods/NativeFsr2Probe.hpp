#pragma once

#include <atomic>
#include <cstring>
#include <memory>
#include <safetyhook.hpp>
#include <sdk/Renderer.hpp>
#include <sdk/REManagedObject.hpp>
#include <utility/Module.hpp>
#include "NativeFsr2GpuProbe.hpp"

// Telemetry only for the exact RE4 executable examined in engineering/reverse_gui_run4.
// No register, input, output, render target, or algorithm changes in the callback.
namespace hudlab::fsr2 {
inline std::unique_ptr<SafetyHookMid> probe;
inline std::atomic<uint32_t> samples{0};
inline bool verified=false;

inline void target(const char* name, sdk::renderer::TargetState* state) {
    if (state == nullptr) return;
    const auto resource=state->get_native_resource_d3d12();
    if (resource == nullptr) return;
    const auto d=resource->GetDesc();
    spdlog::info("[HUDLab/FSR2] {} state={:x} resource={:x} {}x{} format={} flags={:x}",
        name, (uintptr_t)state, (uintptr_t)resource, d.Width, d.Height, (uint32_t)d.Format, (uint32_t)d.Flags);
}

inline void observe(SafetyHookContext& ctx) {
    hudlab::fsr2bindings::arm_native_apply();
    const auto count=samples.fetch_add(1);
    if (count >= 1024 || (count >= 4 && count % 128 != 0)) return;
    try {
        auto scene=(sdk::renderer::layer::Scene*)ctx.rdx;
        const auto type=utility::re_managed_object::get_type_definition(scene);
        if (type == nullptr || type->get_full_name() != "via.render.layer.Scene") {
            spdlog::info("[HUDLab/FSR2] Unexpected second argument; no resource inspection performed");
            return;
        }
        const auto base=(uintptr_t)GetModuleHandleW(nullptr);
        const auto caller=*(uintptr_t*)ctx.rsp;
        spdlog::info("[HUDLab/FSR2] apply sample={} backend={:x} scene={:x} context={:x} arg4={:x} callerRVA={:x} thread={}",
            count,ctx.rcx,ctx.rdx,ctx.r8,ctx.r9,caller-base,GetCurrentThreadId());
        hudlab::fsr2gpu::log_stats();
        // Names are looked up through the existing SDK, never through guessed texture offsets.
        for (auto name : {"HDRTarget", "PostMainTarget", "VelocityTarget", "DepthTarget", "OutputTarget"})
            target(name,scene->get_target_state(name));
    } catch (...) {
        spdlog::info("[HUDLab/FSR2] Resource inspection failed; original function continues unchanged");
    }
}

inline void install() {
#ifdef RE4
    if (probe) return;
    const auto base=(uint8_t*)GetModuleHandleW(nullptr);
    const auto dos=(IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    const auto nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    // SHA256 A1082B154105FAC7CA22668FFC1C99A8BFC9F1B945439276C556EDEA7597A5B7
    // The installer additionally checks the full executable hash.
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != 0x69a8f652 ||
        nt->OptionalHeader.SizeOfImage != 0x0e405000) {
        spdlog::info("[HUDLab/FSR2] Executable version mismatch; probe not installed");
        return;
    }
    constexpr uint8_t expected[]{0x40,0x55,0x41,0x54,0x41,0x55,0x41,0x57,
        0x48,0x8d,0x6c,0x24,0xe8,0x48,0x81,0xec,0x18,0x01,0x00,0x00,
        0x48,0x8b,0x05,0xdd,0x97,0x47,0x08,0x48,0x33,0xc4,0x48,0x89};
    auto entry=base+0x047eec50;
    if (std::memcmp(entry,expected,sizeof(expected)) != 0) {
        spdlog::info("[HUDLab/FSR2] Function bytes differ or another hook is present; probe not installed");
        return;
    }
    verified=true;
    auto hook=safetyhook::create_mid(entry,observe);
    if (hook) {
        probe=std::make_unique<SafetyHookMid>(std::move(hook));
        spdlog::info("[HUDLab/FSR2] Version-checked pass-through probe installed; no render changes");
    }
#endif
}
}
