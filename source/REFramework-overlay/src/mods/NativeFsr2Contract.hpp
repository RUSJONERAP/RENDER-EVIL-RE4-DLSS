#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

// Decoded from the ORIGINAL shader reflection and cb_26486_0.bin, not a guessed
// engine structure. No process pointers, graphics calls, or hard-coded screen size.
namespace hudlab::native_sr {
struct Fsr2Constants {
    int32_t render[2],display[2],luma_size[2],luma_level;
    uint32_t frame;
    float display_rcp[2],jitter[2],projection_jitter[2],padding[2];
    float device_to_view[4],depth_clip_scale[2],lock_scale[2],reactive_rcp[2];
    float downscale[2],pre_exposure,tan_half_fov,jitter_sequence,lock_lifetime;
    float lock_tick,delta_time,dynamic_resolution,luma_rcp;
};
static_assert(sizeof(Fsr2Constants)==144);
static_assert(offsetof(Fsr2Constants,jitter)==40);
static_assert(offsetof(Fsr2Constants,device_to_view)==64);
static_assert(offsetof(Fsr2Constants,pre_exposure)==112);
static_assert(offsetof(Fsr2Constants,delta_time)==132);

inline std::optional<Fsr2Constants> decode(std::span<const std::byte> bytes) {
    if(bytes.size()<sizeof(Fsr2Constants)) return {};
    Fsr2Constants c{};
    std::memcpy(&c,bytes.data(),sizeof(c)); // Works with unaligned upload-buffer offsets.
    for(int i=0;i<2;++i) {
        if(c.render[i]<16 || c.display[i]<c.render[i] || c.display[i]>16384) return {};
        if(!std::isfinite(c.jitter[i]) || std::abs(c.jitter[i])>1.0f ||
           !std::isfinite(c.projection_jitter[i])) return {};
        if(!std::isfinite(c.display_rcp[i]) ||
           std::abs(c.display_rcp[i]*c.display[i]-1.0f)>0.005f) return {};
        if(!std::isfinite(c.downscale[i]) ||
           std::abs(c.downscale[i]-(float)c.render[i]/c.display[i])>0.005f) return {};
    }
    for(float v:c.device_to_view) if(!std::isfinite(v)) return {};
    if(!std::isfinite(c.pre_exposure) || c.pre_exposure<=0 ||
       !std::isfinite(c.tan_half_fov) || c.tan_half_fov<=0 || c.tan_half_fov>100 ||
       !std::isfinite(c.delta_time) || c.delta_time<0 || c.delta_time>10) return {};
    return c;
}

enum class Pass { Unknown,Velocity,Depth,Prepare,Reconstruct,Rcas };
struct Fingerprint { size_t bytes; std::array<uint8_t,16> digest; Pass pass; };
// Generated from the same capture by engineering/export_native_fixture.py.
#include "NativeFsr2Fingerprints.inc"
inline Pass identify(std::span<const std::byte> bytes) {
    if(bytes.size()<20 || std::memcmp(bytes.data(),"DXBC",4)!=0) return Pass::Unknown;
    for(const auto& f:fingerprints)
        if(bytes.size()==f.bytes && std::memcmp(bytes.data()+4,f.digest.data(),16)==0) return f.pass;
    return Pass::Unknown;
}

struct Texture {
    uintptr_t identity{}; // Opaque key only. Resolver must own COM references separately.
    uint64_t width{};
    uint32_t height{},format{},samples{1};
    bool operator==(const Texture&) const = default;
};
struct FrameKey {
    uintptr_t list{};
    uint64_t recording{}; // Increments on successful Reset; address reuse cannot join records.
    uint64_t cb_address{};
    uint32_t frame{};
    bool operator==(const FrameKey&) const = default;
};
enum class Rejection { None, MissingPass, DifferentFrame, WrongDimensions, WrongFormat,
    AliasedOutput, WrongDispatch };
struct Frame {
    FrameKey key{};
    Fsr2Constants constants{};
    Texture color{},motion{},depth{},output{};
    bool velocity_seen{},depth_seen{},prepare_seen{},rcas_seen{},conflict{};

    void observe(FrameKey k,Pass pass,Texture input,Texture out) {
        if(!(key==k)) {conflict=true;return;}
        switch(pass) {
        case Pass::Velocity: motion=out;velocity_seen=true;break;
        case Pass::Depth: depth=out;depth_seen=true;break;
        case Pass::Prepare: color=input;prepare_seen=true;break;
        case Pass::Rcas: output=out;rcas_seen=true;break;
        default:break;
        }
    }
    Rejection validate(uint32_t x,uint32_t y,uint32_t z) const {
        if(conflict) return Rejection::DifferentFrame;
        if(!velocity_seen || !depth_seen || !prepare_seen || !rcas_seen ||
           !color.identity || !motion.identity || !depth.identity || !output.identity)
            return Rejection::MissingPass;
        for(const auto& t:{color,motion,depth})
            if(t.width!=(uint64_t)constants.render[0] || t.height!=(uint32_t)constants.render[1] || t.samples!=1)
                return Rejection::WrongDimensions;
        if(output.width!=(uint64_t)constants.display[0] || output.height!=(uint32_t)constants.display[1] || output.samples!=1)
            return Rejection::WrongDimensions;
        // DXGI formats: R11G11B10_FLOAT=26, R16G16_FLOAT=34, R32_FLOAT=41.
        // Original engine velocity is SNORM=13; the translated texture is FLOAT=34.
        if(color.format!=26 || output.format!=26 || motion.format!=34 || depth.format!=41)
            return Rejection::WrongFormat;
        for(const auto& t:{color,motion,depth}) if(t.identity==output.identity) return Rejection::AliasedOutput;
        if(x!=((uint32_t)constants.display[0]+15)/16 || y!=((uint32_t)constants.display[1]+15)/16 || z!=1)
            return Rejection::WrongDispatch;
        return Rejection::None;
    }
};

struct EvaluationInputs {
    float jitter_x{},jitter_y{},motion_scale_x{},motion_scale_y{},pre_exposure{};
    bool motion_jittered{};
};
inline EvaluationInputs translated_inputs(const Fsr2Constants& c) {
    // CS_UpscalingVelocityTranslate: ((raw.x-projJitter.x)*0.5,
    //                              (raw.y-projJitter.y)*-0.5).
    // These are unjittered UV-space vectors; NGX expects scale to render pixels.
    // Jitter is retained verbatim as a candidate. Its NGX sign still needs live validation.
    return {c.jitter[0],c.jitter[1],(float)c.render[0],(float)c.render[1],c.pre_exposure,false};
}
}
