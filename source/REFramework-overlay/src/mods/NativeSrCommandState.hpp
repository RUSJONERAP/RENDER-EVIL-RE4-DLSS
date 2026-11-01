#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// API-independent state recorder. The runtime adapter must hold COM references to
// these opaque identities for the duration of recording/restoration. Never owns or
// queues a raw engine RenderContext. Not connected to an Evaluate hook yet.
namespace hudlab::native_sr {
enum class RootKind { Table,Constants,Cbv,Srv,Uav };
struct RootSpec {RootKind kind{};uint32_t words{};};
struct RootLayout {
    std::vector<RootSpec> parameters;
    bool valid() const {
        if(parameters.size()>64) return false;
        uint32_t budget=0;
        for(const auto& p:parameters) {
            if(p.kind==RootKind::Constants && (p.words==0 || p.words>64)) return false;
            budget+=p.kind==RootKind::Constants?p.words:(p.kind==RootKind::Table?1:2);
        }
        return budget<=64;
    }
};
struct RootValue {
    uint64_t address{};
    bool bound{};
    std::array<uint32_t,64> words{};
    uint64_t initialized{};
};
struct RootState {
    uintptr_t signature{};
    RootLayout layout;
    std::array<RootValue,64> values{};
    bool known{};
    void set_signature(uintptr_t identity,const RootLayout* description) {
        if(known && signature==identity) return; // D3D12: setting the same signature preserves arguments.
        *this={};signature=identity;
        if(identity && description && description->valid()) {layout=*description;known=true;}
    }
    bool set_address(uint32_t slot,RootKind kind,uint64_t address) {
        if(!known || slot>=layout.parameters.size() || layout.parameters[slot].kind!=kind || kind==RootKind::Constants) return false;
        values[slot].address=address;values[slot].bound=true;return true;
    }
    bool set_constants(uint32_t slot,uint32_t offset,std::span<const uint32_t> words) {
        if(!known || slot>=layout.parameters.size()) return false;
        const auto& spec=layout.parameters[slot];
        if(spec.kind!=RootKind::Constants || offset>spec.words || words.size()>spec.words-offset) return false;
        for(size_t i=0;i<words.size();++i) {
            values[slot].words[offset+i]=words[i];
            values[slot].initialized|=uint64_t{1}<<(offset+i);
        }
        return true;
    }
    void invalidate_tables() {
        for(size_t i=0;i<layout.parameters.size();++i)
            if(layout.parameters[i].kind==RootKind::Table) values[i].bound=false;
    }
};
enum class RestoreKind {Heaps,ComputeSignature,GraphicsSignature,ComputeArgument,GraphicsArgument,Pipeline};
struct RestoreOp {
    RestoreKind kind{};
    RootKind root_kind{};
    uint32_t slot{},offset{};
    uint64_t value{};
    std::vector<uint32_t> words;
    std::vector<uintptr_t> heaps;
};
struct CommandState {
    uint64_t recording{};
    uintptr_t pipeline{};
    RootState compute{},graphics{};
    std::vector<uintptr_t> heaps;
    bool reset_seen{},heaps_known{},unsupported{};

    void reset(uintptr_t initial_pipeline) {
        const auto next=recording+1;
        *this={};recording=next;pipeline=initial_pipeline;reset_seen=true;
    }
    bool set_heaps(std::span<const uintptr_t> next) {
        if(next.size()>2) {unsupported=true;return false;}
        const std::vector<uintptr_t> updated(next.begin(),next.end());
        if(!heaps_known || heaps!=updated) {compute.invalidate_tables();graphics.invalidate_tables();}
        heaps=updated;heaps_known=true;return true;
    }
    void execute_bundle() {unsupported=true;} // No assumptions about inherited bundle state.
    void clear_state(uintptr_t initial_pipeline) {
        compute={};graphics={};heaps.clear();heaps_known=false;pipeline=initial_pipeline;
    }
    bool restorable() const {
        return reset_seen && !unsupported && heaps_known && pipeline && compute.known &&
            (!graphics.signature || graphics.known);
    }
    std::vector<RestoreOp> restore_plan() const {
        if(!restorable()) return {};
        std::vector<RestoreOp> result;
        RestoreOp heap;heap.kind=RestoreKind::Heaps;heap.heaps=heaps;result.push_back(heap);
        const auto append=[&](const RootState& state,bool gfx) {
            if(!state.signature) return;
            RestoreOp sig;sig.kind=gfx?RestoreKind::GraphicsSignature:RestoreKind::ComputeSignature;
            sig.value=state.signature;result.push_back(sig);
            for(size_t i=0;i<state.layout.parameters.size();++i) {
                const auto& v=state.values[i];const auto& p=state.layout.parameters[i];
                const auto kind=gfx?RestoreKind::GraphicsArgument:RestoreKind::ComputeArgument;
                if(p.kind==RootKind::Constants) {
                    // Restore initialized runs only. Never write a fabricated zero into an
                    // uninitialized root constant that was not present in the original recording.
                    uint32_t offset=0;
                    while(offset<p.words) {
                        if(!(v.initialized&(uint64_t{1}<<offset))) {++offset;continue;}
                        RestoreOp op;op.kind=kind;op.root_kind=p.kind;op.slot=(uint32_t)i;op.offset=offset;
                        do {op.words.push_back(v.words[offset++]);}
                        while(offset<p.words && (v.initialized&(uint64_t{1}<<offset)));
                        result.push_back(op);
                    }
                } else if(v.bound) {
                    RestoreOp op;op.kind=kind;op.root_kind=p.kind;op.slot=(uint32_t)i;op.value=v.address;result.push_back(op);
                }
            }
        };
        append(compute,false);append(graphics,true);
        RestoreOp pso;pso.kind=RestoreKind::Pipeline;pso.value=pipeline;result.push_back(pso);
        return result;
    }
};
}
