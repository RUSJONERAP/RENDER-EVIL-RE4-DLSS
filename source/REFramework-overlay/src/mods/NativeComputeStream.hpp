#pragma once
#include <windows.h>
#include <d3d12.h>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>

namespace hudlab::native_sr {
// The public D3D12 stream ABI aligns every (tag,payload) subobject to a pointer.
// Deliberately accepts compute-only streams. Unknown/new tags are NOT stepped over
// with guessed sizes. Original API calls always continue regardless of this result.
template<class T> struct alignas(void*) StreamPart {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
    T payload;
};
inline std::optional<D3D12_SHADER_BYTECODE> compute_stream(std::span<const std::byte> stream) {
    if(stream.empty() || stream.size()>65536) return {};
    size_t offset=0;
    uint64_t seen=0;
    std::optional<D3D12_SHADER_BYTECODE> shader;
    while(offset<stream.size()) {
        if(stream.size()-offset<sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE)) return {};
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        std::memcpy(&type,stream.data()+offset,sizeof(type));
        if((uint32_t)type>=64 || (seen&(uint64_t{1}<<(uint32_t)type))) return {};
        seen|=uint64_t{1}<<(uint32_t)type;
        size_t bytes=0;
        switch(type) {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: {
            using Part=StreamPart<D3D12_SHADER_BYTECODE>;
            bytes=sizeof(Part);
            if(bytes>stream.size()-offset) return {};
            D3D12_SHADER_BYTECODE cs{};
            std::memcpy(&cs,stream.data()+offset+offsetof(Part,payload),sizeof(cs));
            if(!cs.pShaderBytecode || cs.BytecodeLength<20) return {};
            shader=cs;break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: bytes=sizeof(StreamPart<ID3D12RootSignature*>);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: bytes=sizeof(StreamPart<UINT>);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: bytes=sizeof(StreamPart<D3D12_CACHED_PIPELINE_STATE>);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: bytes=sizeof(StreamPart<D3D12_PIPELINE_STATE_FLAGS>);break;
        default:return {};
        }
        if(bytes>stream.size()-offset) return {};
        offset+=bytes;
    }
    return shader;
}
}
