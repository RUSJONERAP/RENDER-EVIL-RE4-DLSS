#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <atomic>
#include <mutex>
#include <utility/FunctionHook.hpp>
#include "NativeSrBindings.hpp"

// Read-only binding observer. Texture pointers are copied as opaque identities with
// immutable GetDesc snapshots, NEVER dereferenced later or passed into an upscaler.
// Only mapped CPU UPLOAD buffers are retained, capped at 512 MiB; no VRAM readback.
namespace hudlab::fsr2bindings {
using namespace native_sr;
using Microsoft::WRL::ComPtr;
inline std::atomic<bool> enabled{true};
inline std::atomic<uint32_t> completed{0},incomplete{0};
inline std::mutex mutex;
inline DescriptorSnapshots descriptors;
inline std::unordered_map<uintptr_t,BindingLayout> layouts;
inline std::unordered_map<uintptr_t,BindingCommands> commands;
inline std::unique_ptr<FunctionHook> root_hook,heap_hook,srv_hook,uav_hook,cbv_hook,copy_hook,simple_hook;
inline std::unique_ptr<FunctionHook> sig_hook,table_hook,address_hook,heaps_hook,map_hook,unmap_hook;
inline std::unique_ptr<FunctionHook> clear_hook,bundle_hook;
inline uint32_t descriptor_stride=0;
using Deserialize=HRESULT(WINAPI*)(LPCVOID,SIZE_T,REFIID,void**);
inline Deserialize deserialize=nullptr;
// Installed by the shader observer before these hooks are published. ClearState
// changes the active PSO; a bundle makes the parent's shadow PSO unknowable.
inline void (*pipeline_changed)(ID3D12GraphicsCommandList*,ID3D12PipelineState*)=nullptr;
struct Upload {
    ComPtr<ID3D12Resource> owner;
    uint64_t address{},bytes{};
    const std::byte* mapped{};
};
inline std::vector<Upload> uploads;
inline uint64_t upload_bytes=0;
inline std::atomic<uint64_t> absolute_deadline{0},apply_deadline{0};
inline void clear_metadata_locked() {
    descriptors.clear();commands.clear();layouts.clear();uploads.clear();upload_bytes=0;
}
inline void arm_native_apply() {
    uint64_t expected=0;
    apply_deadline.compare_exchange_strong(expected,GetTickCount64()+60000);
}

template<class F> inline void observe(F&& function) {
    if(!enabled.load(std::memory_order_relaxed)) return;
    try {
        const auto now=GetTickCount64();const auto deadline=apply_deadline.load();
        if((absolute_deadline.load() && now>=absolute_deadline.load()) || (deadline && now>=deadline)) {
            if(enabled.exchange(false)) {
                std::scoped_lock lock{mutex};clear_metadata_locked();
                spdlog::info("[HUDLab/Bindings] Diagnostic time limit reached; tracking stopped and retained CPU upload references released");
            }
            return;
        }
        function();
    }
    catch(...) {
        enabled.store(false);
        {std::scoped_lock lock{mutex};clear_metadata_locked();}
        spdlog::warn("[HUDLab/Bindings] Telemetry exception; observer stopped, original rendering continues");
    }
}
inline BindingCommands* command(uintptr_t list) {
    auto found=commands.find(list);
    if(found!=commands.end()) return &found->second;
    if(commands.size()>=1024) return nullptr;
    return &commands[list];
}
inline BindingKind kind(D3D12_DESCRIPTOR_RANGE_TYPE value) {
    switch(value) {
    case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:return BindingKind::Srv;
    case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:return BindingKind::Uav;
    case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:return BindingKind::Cbv;
    default:return BindingKind::Sampler;
    }
}
inline void record_root(uintptr_t identity,const void* bytes,SIZE_T size) {
    if(!deserialize || !bytes || !size || size>1024*1024) return;
    ComPtr<ID3D12VersionedRootSignatureDeserializer> reader;
    if(FAILED(deserialize(bytes,size,IID_PPV_ARGS(&reader)))) return;
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc=nullptr;
    if(FAILED(reader->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0,&desc)) || !desc || desc->Desc_1_0.NumParameters>64) return;
    BindingLayout layout;
    for(UINT i=0;i<desc->Desc_1_0.NumParameters;++i) {
        const auto& parameter=desc->Desc_1_0.pParameters[i];BindingRoot root;
        if(parameter.ParameterType==D3D12_ROOT_PARAMETER_TYPE_CBV) {
            root.direct_cbv=true;root.reg=parameter.Descriptor.ShaderRegister;root.space=parameter.Descriptor.RegisterSpace;
        } else if(parameter.ParameterType==D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            root.table=true;uint64_t end=0;
            if(parameter.DescriptorTable.NumDescriptorRanges>128) return;
            for(UINT j=0;j<parameter.DescriptorTable.NumDescriptorRanges;++j) {
                const auto& range=parameter.DescriptorTable.pDescriptorRanges[j];
                const uint64_t offset=range.OffsetInDescriptorsFromTableStart==D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND?end:range.OffsetInDescriptorsFromTableStart;
                root.ranges.push_back({kind(range.RangeType),range.BaseShaderRegister,range.RegisterSpace,range.NumDescriptors,offset});
                end=offset+range.NumDescriptors;
            }
        }
        layout.roots.push_back(std::move(root));
    }
    std::scoped_lock lock{mutex};if(!enabled.load()) return;
    if(layouts.size()<4096 || layouts.contains(identity)) layouts.insert_or_assign(identity,std::move(layout));
}
inline HRESULT STDMETHODCALLTYPE create_root(ID3D12Device* device,UINT node,const void* bytes,SIZE_T size,REFIID iid,void** result) {
    const auto hr=root_hook->get_original<decltype(create_root)>()(device,node,bytes,size,iid,result);
    observe([&]{if(SUCCEEDED(hr) && result && *result) record_root((uintptr_t)*result,bytes,size);});return hr;
}
inline HRESULT STDMETHODCALLTYPE create_heap(ID3D12Device* device,const D3D12_DESCRIPTOR_HEAP_DESC* desc,REFIID iid,void** result) {
    const auto hr=heap_hook->get_original<decltype(create_heap)>()(device,desc,iid,result);
    observe([&]{
        if(FAILED(hr) || !desc || desc->Type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || !result || !*result) return;
        ComPtr<ID3D12DescriptorHeap> heap;
        if(FAILED(((IUnknown*)*result)->QueryInterface(IID_PPV_ARGS(&heap)))) return;
        const auto cpu=heap->GetCPUDescriptorHandleForHeapStart().ptr;
        const auto gpu=(desc->Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)?heap->GetGPUDescriptorHandleForHeapStart().ptr:0;
        std::scoped_lock lock{mutex};if(enabled.load()) descriptors.heap({(uintptr_t)heap.Get(),cpu,gpu,desc->NumDescriptors,descriptor_stride});
    });return hr;
}
inline std::optional<Texture> texture(ID3D12Resource* resource,DXGI_FORMAT format,uint32_t mip) {
    if(!resource) return {};
    const auto desc=resource->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize!=1 || mip>=desc.MipLevels || mip>=32) return {};
    return Texture{(uintptr_t)resource,std::max<uint64_t>(1,desc.Width>>mip),std::max(1u,desc.Height>>mip),
        (uint32_t)(format==DXGI_FORMAT_UNKNOWN?desc.Format:format),desc.SampleDesc.Count};
}
inline void STDMETHODCALLTYPE create_srv(ID3D12Device* device,ID3D12Resource* resource,const D3D12_SHADER_RESOURCE_VIEW_DESC* desc,D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    srv_hook->get_original<decltype(create_srv)>()(device,resource,desc,handle);
    observe([&]{
        auto t=(!desc || desc->ViewDimension==D3D12_SRV_DIMENSION_TEXTURE2D)?texture(resource,desc?desc->Format:DXGI_FORMAT_UNKNOWN,desc?desc->Texture2D.MostDetailedMip:0):std::optional<Texture>{};
        std::scoped_lock lock{mutex};if(enabled.load()) descriptors.view(handle.ptr,{BindingKind::Srv,t.value_or(Texture{}),0,0});
    });
}
inline void STDMETHODCALLTYPE create_uav(ID3D12Device* device,ID3D12Resource* resource,ID3D12Resource* counter,const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc,D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    uav_hook->get_original<decltype(create_uav)>()(device,resource,counter,desc,handle);
    observe([&]{
        auto t=(!desc || desc->ViewDimension==D3D12_UAV_DIMENSION_TEXTURE2D)?texture(resource,desc?desc->Format:DXGI_FORMAT_UNKNOWN,desc?desc->Texture2D.MipSlice:0):std::optional<Texture>{};
        std::scoped_lock lock{mutex};if(enabled.load()) descriptors.view(handle.ptr,{BindingKind::Uav,t.value_or(Texture{}),0,0});
    });
}
inline void STDMETHODCALLTYPE create_cbv(ID3D12Device* device,const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc,D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    cbv_hook->get_original<decltype(create_cbv)>()(device,desc,handle);
    observe([&]{std::scoped_lock lock{mutex};if(enabled.load()) descriptors.view(handle.ptr,{BindingKind::Cbv,{},desc?desc->BufferLocation:0,desc?desc->SizeInBytes:0});});
}
inline void STDMETHODCALLTYPE copy_simple(ID3D12Device* device,UINT count,D3D12_CPU_DESCRIPTOR_HANDLE destination,D3D12_CPU_DESCRIPTOR_HANDLE source,D3D12_DESCRIPTOR_HEAP_TYPE type) {
    observe([&]{
        if(type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) return;
        auto dst=expand_descriptor_ranges(std::array<uint64_t,1>{destination.ptr},std::array<uint32_t,1>{count},descriptor_stride);
        auto src=expand_descriptor_ranges(std::array<uint64_t,1>{source.ptr},std::array<uint32_t,1>{count},descriptor_stride);
        if(dst && src) {std::scoped_lock lock{mutex};if(enabled.load()) descriptors.copy(*dst,*src);}
    });
    simple_hook->get_original<decltype(copy_simple)>()(device,count,destination,source,type);
}
inline void STDMETHODCALLTYPE copy_descriptors(ID3D12Device* device,UINT dst_count,const D3D12_CPU_DESCRIPTOR_HANDLE* dst,const UINT* dst_sizes,UINT src_count,const D3D12_CPU_DESCRIPTOR_HANDLE* src,const UINT* src_sizes,D3D12_DESCRIPTOR_HEAP_TYPE type) {
    observe([&]{
        if(type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || dst_count>8192 || src_count>8192 || !dst || !src) return;
        std::vector<uint64_t> d,s;for(UINT i=0;i<dst_count;++i) d.push_back(dst[i].ptr);for(UINT i=0;i<src_count;++i) s.push_back(src[i].ptr);
        auto a=expand_descriptor_ranges(d,dst_sizes?std::span<const uint32_t>{dst_sizes,dst_count}:std::span<const uint32_t>{},descriptor_stride);
        auto b=expand_descriptor_ranges(s,src_sizes?std::span<const uint32_t>{src_sizes,src_count}:std::span<const uint32_t>{},descriptor_stride);
        if(a && b) {std::scoped_lock lock{mutex};if(enabled.load()) descriptors.copy(*a,*b);}
    });
    copy_hook->get_original<decltype(copy_descriptors)>()(device,dst_count,dst,dst_sizes,src_count,src,src_sizes,type);
}
inline void STDMETHODCALLTYPE set_signature(ID3D12GraphicsCommandList* list,ID3D12RootSignature* root) {
    sig_hook->get_original<decltype(set_signature)>()(list,root);
    observe([&]{std::scoped_lock lock{mutex};if(!enabled.load())return;if(auto c=command((uintptr_t)list)) c->root_signature((uintptr_t)root);});
}
inline void STDMETHODCALLTYPE set_table(ID3D12GraphicsCommandList* list,UINT index,D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    table_hook->get_original<decltype(set_table)>()(list,index,handle);
    observe([&]{if(index>=64)return;std::scoped_lock lock{mutex};if(!enabled.load())return;if(auto c=command((uintptr_t)list)) c->tables[index]=handle.ptr;});
}
inline void STDMETHODCALLTYPE set_cbv(ID3D12GraphicsCommandList* list,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address) {
    address_hook->get_original<decltype(set_cbv)>()(list,index,address);
    observe([&]{if(index>=64)return;std::scoped_lock lock{mutex};if(!enabled.load())return;if(auto c=command((uintptr_t)list)) c->cbv[index]=address;});
}
inline void STDMETHODCALLTYPE set_heaps(ID3D12GraphicsCommandList* list,UINT count,ID3D12DescriptorHeap* const* heaps) {
    heaps_hook->get_original<decltype(set_heaps)>()(list,count,heaps);
    observe([&]{
        if(count>2 || (count && !heaps)) return;
        std::vector<uintptr_t> ids;for(UINT i=0;i<count;++i) ids.push_back((uintptr_t)heaps[i]);
        std::scoped_lock lock{mutex};if(!enabled.load())return;if(auto c=command((uintptr_t)list)) c->descriptor_heaps(ids);
    });
}
inline HRESULT STDMETHODCALLTYPE map(ID3D12Resource* resource,UINT subresource,const D3D12_RANGE* range,void** data) {
    const auto hr=map_hook->get_original<decltype(map)>()(resource,subresource,range,data);
    observe([&]{
        if(FAILED(hr) || subresource || !data || !*data) return;
        const auto desc=resource->GetDesc();D3D12_HEAP_PROPERTIES heap{};D3D12_HEAP_FLAGS flags{};
        if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || desc.Width<1024*1024 || desc.Width>256ull*1024*1024 ||
           FAILED(resource->GetHeapProperties(&heap,&flags)) || heap.Type!=D3D12_HEAP_TYPE_UPLOAD) return;
        std::scoped_lock lock{mutex};if(!enabled.load()) return;
        for(auto& u:uploads) if(u.owner.Get()==resource) {u.mapped=(const std::byte*)*data;return;}
        if(uploads.size()>=32 || desc.Width>512ull*1024*1024-upload_bytes) return;
        Upload u;u.owner=resource;u.address=resource->GetGPUVirtualAddress();u.bytes=desc.Width;u.mapped=(const std::byte*)*data;
        if(u.address) {upload_bytes+=u.bytes;uploads.push_back(std::move(u));}
    });return hr;
}
inline void STDMETHODCALLTYPE unmap(ID3D12Resource* resource,UINT subresource,const D3D12_RANGE* range) {
    // Invalidate before Unmap can invalidate the pointer. Copying constants holds this mutex.
    observe([&]{if(subresource)return;std::scoped_lock lock{mutex};for(auto& u:uploads) if(u.owner.Get()==resource) u.mapped=nullptr;});
    unmap_hook->get_original<decltype(unmap)>()(resource,subresource,range);
}
inline std::optional<Fsr2Constants> constants(uint64_t address) {
    const std::byte* data=nullptr;
    for(const auto& u:uploads) {
        if(!u.mapped || address<u.address || u.bytes<sizeof(Fsr2Constants) || address-u.address>u.bytes-sizeof(Fsr2Constants)) continue;
        if(data) return {}; // Ambiguous GPU-VA alias: do not choose an arbitrary upload owner.
        data=u.mapped+(address-u.address);
    }
    return data?decode({data,sizeof(Fsr2Constants)}):std::optional<Fsr2Constants>{};
}
inline void on_reset(uintptr_t list) {observe([&]{std::scoped_lock lock{mutex};if(!enabled.load())return;if(auto c=command(list)) c->reset();});}
inline void STDMETHODCALLTYPE clear_state(ID3D12GraphicsCommandList* list,ID3D12PipelineState* pso) {
    clear_hook->get_original<decltype(clear_state)>()(list,pso);
    if(pipeline_changed) pipeline_changed(list,pso);
    on_reset((uintptr_t)list);
}
inline void STDMETHODCALLTYPE execute_bundle(ID3D12GraphicsCommandList* list,ID3D12GraphicsCommandList* bundle) {
    bundle_hook->get_original<decltype(execute_bundle)>()(list,bundle);
    if(pipeline_changed) pipeline_changed(list,nullptr);
    observe([&]{std::scoped_lock lock{mutex};if(!enabled.load())return;
        if(auto c=command((uintptr_t)list)) {c->unsupported_bundle=true;c->frame_started=false;}});
}

inline void before_dispatch(uintptr_t list,Pass pass,UINT x,UINT y,UINT z) {
    if(pass==Pass::Unknown) return;
    observe([&]{
        std::scoped_lock lock{mutex};if(!enabled.load()) return;auto c=command(list);if(!c || c->unsupported_bundle) return;
        auto layout=layouts.find(c->signature);if(layout==layouts.end()) return;
        const auto get=[&](BindingKind k,UINT reg){return c->resolve(layout->second,descriptors,k,reg);};
        const auto cbv=get(BindingKind::Cbv,0);
        const auto cb=cbv?constants(cbv->cb_address):std::optional<Fsr2Constants>{};
        if(pass==Pass::Velocity) {
            c->frame_started=false;c->reconstruction_verified=false;
            const auto out=get(BindingKind::Uav,0);
            if(cb && out && c->recording) {
                c->frame={};c->frame.key={list,c->recording,cbv->cb_address,cb->frame};c->frame.constants=*cb;
                c->frame.observe(c->frame.key,pass,{},out->texture);c->frame_started=true;
            }
        } else if(c->frame_started) {
            const auto frame_key=cb?FrameKey{list,c->recording,cbv->cb_address,cb->frame}:c->frame.key;
            if(pass==Pass::Depth && c->last_pass==Pass::Velocity) {
                if(auto out=get(BindingKind::Uav,0)) c->frame.observe(frame_key,pass,{},out->texture);
            } else if(pass==Pass::Prepare && cb) {
                if(auto input=get(BindingKind::Srv,0)) c->frame.observe(frame_key,pass,input->texture,{});
            } else if(pass==Pass::Reconstruct && cb) {
                auto motion=get(BindingKind::Srv,0),depth=get(BindingKind::Srv,1);
                c->reconstruction_verified=frame_key==c->frame.key && motion && depth && motion->texture==c->frame.motion && depth->texture==c->frame.depth;
            } else if(pass==Pass::Rcas && cb) {
                if(auto out=get(BindingKind::Uav,0)) c->frame.observe(frame_key,pass,{},out->texture);
                const auto result=c->frame.validate(x,y,z);
                if(cb->render[0]>=1280) {
                    if(result==Rejection::None && c->reconstruction_verified) {
                        const auto n=completed.fetch_add(1);
                        if(n<4) {
                            spdlog::info("[HUDLab/Bindings] Complete native frame={} recording={} CBV={:x} {}x{} -> {}x{} jitter={},{} exposure={} (metadata only, no NGX)",
                                cb->frame,c->recording,cbv->cb_address,cb->render[0],cb->render[1],cb->display[0],cb->display[1],cb->jitter[0],cb->jitter[1],cb->pre_exposure);
                            for(auto pair:{std::pair{"color",c->frame.color},std::pair{"motion",c->frame.motion},std::pair{"depth",c->frame.depth},std::pair{"output",c->frame.output}})
                                spdlog::info("[HUDLab/Bindings] {} resource={:x} {}x{} format={}",pair.first,pair.second.identity,pair.second.width,pair.second.height,pair.second.format);
                        }
                        if(n>=7) {enabled.store(false);clear_metadata_locked();
                            spdlog::info("[HUDLab/Bindings] Eight validated native frames collected; metadata tracking stopped and retained CPU upload references released");return;}
                    } else if(incomplete.fetch_add(1)<4) spdlog::info("[HUDLab/Bindings] Incomplete native frame={} rejection={} reconstructed={}",cb->frame,(int)result,c->reconstruction_verified);
                }
                c->frame_started=false;
            }
        }
        c->last_pass=pass;
    });
}
inline void log_stats() {
    std::scoped_lock lock{mutex};
    spdlog::info("[HUDLab/Bindings] layouts={} commandLists={} uploadBuffers={} retainedMiB={} complete={} incomplete={} tracking={}",
        layouts.size(),commands.size(),uploads.size(),upload_bytes/(1024*1024),completed.load(),incomplete.load(),enabled.load());
}
inline void install(ID3D12Device* device,const uintptr_t* dv,const uintptr_t* cv) {
    absolute_deadline.store(GetTickCount64()+10*60*1000);
    const auto d3d12=GetModuleHandleW(L"d3d12.dll");deserialize=(Deserialize)GetProcAddress(d3d12,"D3D12CreateVersionedRootSignatureDeserializer");
    descriptor_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto hook=[](auto& storage,uintptr_t target,auto callback){storage=std::make_unique<FunctionHook>(target,(uintptr_t)callback);return storage->create();};
    bool ok=deserialize && descriptor_stride;
    ok=hook(root_hook,dv[16],&create_root)&&ok;ok=hook(heap_hook,dv[14],&create_heap)&&ok;
    ok=hook(srv_hook,dv[18],&create_srv)&&ok;ok=hook(uav_hook,dv[19],&create_uav)&&ok;ok=hook(cbv_hook,dv[17],&create_cbv)&&ok;
    ok=hook(copy_hook,dv[23],&copy_descriptors)&&ok;ok=hook(simple_hook,dv[24],&copy_simple)&&ok;
    ok=hook(sig_hook,cv[29],&set_signature)&&ok;ok=hook(table_hook,cv[31],&set_table)&&ok;
    ok=hook(address_hook,cv[37],&set_cbv)&&ok;ok=hook(heaps_hook,cv[28],&set_heaps)&&ok;
    ok=hook(clear_hook,cv[11],&clear_state)&&ok;ok=hook(bundle_hook,cv[27],&execute_bundle)&&ok;
    ComPtr<ID3D12Resource> buffer;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=65536;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&buffer)))) {
        const auto rv=*(uintptr_t**)buffer.Get();ok=hook(map_hook,rv[8],&map)&&ok;ok=hook(unmap_hook,rv[9],&unmap)&&ok;
    } else ok=false;
    if(!ok) enabled.store(false); // Keep any installed trampolines alive on partial setup failure.
    spdlog::info("[HUDLab/Bindings] Read-only binding observer setup={} (no GPU resource writes or evaluation)",ok);
}
}
