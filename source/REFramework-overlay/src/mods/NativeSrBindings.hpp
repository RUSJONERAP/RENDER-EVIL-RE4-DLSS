#pragma once
#include "NativeFsr2Contract.hpp"
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <vector>

namespace hudlab::native_sr {
enum class BindingKind {Srv,Uav,Cbv,Sampler};
struct ViewSnapshot {
    BindingKind kind{};
    Texture texture{}; // Values captured while the API's resource argument is alive.
    uint64_t cb_address{};
    uint32_t cb_bytes{};
};
struct HeapSnapshot {
    uintptr_t identity{};
    uint64_t cpu{},gpu{};
    uint32_t count{},stride{};
};
class DescriptorSnapshots {
public:
    static constexpr size_t max_heaps=4096,max_views=262144;
    bool heap(HeapSnapshot h) {
        if(!h.cpu || !h.count || !h.stride || (uint64_t)h.count*h.stride>std::numeric_limits<uint64_t>::max()-h.cpu) return false;
        if(h.gpu && (uint64_t)h.count*h.stride>std::numeric_limits<uint64_t>::max()-h.gpu) return false;
        if(heaps.size()>=max_heaps && !heaps.contains(h.identity)) return false;
        // A CPU descriptor range can be reused after a heap is destroyed. Never retain
        // its old descriptor values into the new heap generation.
        for(auto it=views.begin();it!=views.end();) {
            if(it->first>=h.cpu && it->first-h.cpu<(uint64_t)h.count*h.stride) it=views.erase(it);else ++it;
        }
        heaps[h.identity]=h;return true;
    }
    bool view(uint64_t cpu,ViewSnapshot snapshot) {
        if(!cpu || (views.size()>=max_views && !views.contains(cpu))) return false;
        views.insert_or_assign(cpu,snapshot);return true;
    }
    std::optional<ViewSnapshot> at_cpu(uint64_t cpu) const {
        auto found=views.find(cpu);return found==views.end()?std::optional<ViewSnapshot>{}:found->second;
    }
    std::optional<ViewSnapshot> at_gpu(uint64_t gpu,std::span<const uintptr_t> bound_heaps) const {
        std::optional<uint64_t> cpu;
        for(auto identity:bound_heaps) {
            auto found=heaps.find(identity);if(found==heaps.end()) continue;
            const auto& h=found->second;
            if(!h.gpu || gpu<h.gpu) continue;
            const auto delta=gpu-h.gpu;
            if(delta%h.stride || delta/h.stride>=h.count) continue;
            if(cpu) return {}; // Ambiguous overlap: never choose a guessed heap.
            cpu=h.cpu+delta;
        }
        return cpu?at_cpu(*cpu):std::optional<ViewSnapshot>{};
    }
    std::optional<uint64_t> offset_gpu(uint64_t gpu,uint64_t offset,std::span<const uintptr_t> bound_heaps) const {
        std::optional<uint64_t> result;
        for(auto identity:bound_heaps) {
            auto found=heaps.find(identity);if(found==heaps.end()) continue;
            const auto& h=found->second;
            if(!h.gpu || gpu<h.gpu) continue;
            const auto delta=gpu-h.gpu;
            if(delta%h.stride || delta/h.stride>=h.count || offset>=h.count-delta/h.stride) continue;
            if(result) return {};
            result=gpu+offset*h.stride;
        }
        return result;
    }
    bool copy(std::span<const uint64_t> destinations,std::span<const uint64_t> sources) {
        if(destinations.size()!=sources.size() || destinations.size()>8192) return false;
        std::vector<std::optional<ViewSnapshot>> values;values.reserve(sources.size());
        for(auto source:sources) values.push_back(at_cpu(source));
        for(size_t i=0;i<destinations.size();++i) {
            if(values[i]) {if(!view(destinations[i],*values[i])) return false;}
            else views.erase(destinations[i]); // Unknown source invalidates a previously known destination.
        }
        return true;
    }
    void clear() {heaps.clear();views.clear();}
private:
    std::unordered_map<uintptr_t,HeapSnapshot> heaps;
    std::unordered_map<uint64_t,ViewSnapshot> views;
};
struct TableRange {
    BindingKind kind{};
    uint32_t reg{},space{},count{};
    uint64_t offset{}; // Descriptor count, not bytes; APPEND resolved by the API adapter.
};
struct BindingRoot {
    bool table{},direct_cbv{};
    uint32_t reg{},space{};
    std::vector<TableRange> ranges;
};
struct BindingLayout {std::vector<BindingRoot> roots;};
struct BindingCommands {
    uint64_t recording{};
    uintptr_t signature{};
    std::array<uint64_t,64> tables{},cbv{};
    std::vector<uintptr_t> heaps;
    Frame frame{};
    bool frame_started{},reconstruction_verified{};
    bool unsupported_bundle{};
    Pass last_pass{Pass::Unknown};
    void reset() {const auto next=recording+1;*this={};recording=next;}
    void root_signature(uintptr_t next) {
        if(next!=signature) {tables={};cbv={};signature=next;}
    }
    void descriptor_heaps(std::span<const uintptr_t> next) {
        const std::vector<uintptr_t> ids(next.begin(),next.end());
        if(heaps!=ids) tables={}; // Root CBVs remain valid when heaps change.
        heaps=ids;
    }
    std::optional<ViewSnapshot> resolve(const BindingLayout& layout,const DescriptorSnapshots& store,BindingKind kind,uint32_t reg,uint32_t space=0) const {
        std::optional<ViewSnapshot> result;
        for(size_t i=0;i<layout.roots.size() && i<64;++i) {
            const auto& root=layout.roots[i];
            if(root.direct_cbv && kind==BindingKind::Cbv && root.reg==reg && root.space==space && cbv[i]) {
                if(result) return {};
                result=ViewSnapshot{BindingKind::Cbv,{},cbv[i],0};
            }
            if(!root.table || !tables[i]) continue;
            for(const auto& range:root.ranges) {
                if(range.kind!=kind || range.space!=space || reg<range.reg || (uint64_t)reg-range.reg>=range.count) continue;
                auto handle=store.offset_gpu(tables[i],range.offset+reg-range.reg,heaps);
                auto view=handle?store.at_gpu(*handle,heaps):std::optional<ViewSnapshot>{};
                if(!view || view->kind!=kind || result) return {};
                result=*view;
            }
        }
        return result;
    }
};
inline std::optional<std::vector<uint64_t>> expand_descriptor_ranges(std::span<const uint64_t> starts,std::span<const uint32_t> sizes,uint32_t stride) {
    if(!stride || (!sizes.empty() && starts.size()!=sizes.size())) return {};
    std::vector<uint64_t> result;
    for(size_t i=0;i<starts.size();++i) {
        const auto count=sizes.empty()?1:sizes[i];
        if(count>8192-result.size() || !starts[i] ||
           (uint64_t)count*stride>std::numeric_limits<uint64_t>::max()-starts[i]) return {};
        for(uint32_t j=0;j<count;++j) result.push_back(starts[i]+(uint64_t)j*stride);
    }
    return result;
}
}
