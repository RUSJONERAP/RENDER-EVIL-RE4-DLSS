#pragma once
#include "UnifiedRuntime.hpp"
#include <d3d12.h>
#include <wrl/client.h>
#include <atomic>
#include <sstream>

namespace re4unified::gpu_diagnostics {
inline std::atomic<bool> captured=false;
inline void enable() {
    if(!active())return;
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings;
    const auto result=D3D12GetDebugInterface(IID_PPV_ARGS(&settings));
    if(SUCCEEDED(result)) {
        settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        settings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
    std::ostringstream text;text<<"DRED setup result: 0x"<<std::hex<<(uint32_t)result<<"\n";
    write_file(root()/L"reframework/data/re4unified/gpu_diagnostics.txt",text.str());
}
inline void inspect(ID3D12Device* device) {
    if(!active() || !device || captured.load())return;
    const auto removed=device->GetDeviceRemovedReason();
    if(SUCCEEDED(removed) || captured.exchange(true))return;
    std::ostringstream text;text<<"Device removed: 0x"<<std::hex<<(uint32_t)removed<<"\n";
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    const auto query=device->QueryInterface(IID_PPV_ARGS(&dred));
    text<<"DRED query: 0x"<<(uint32_t)query<<"\n";
    if(SUCCEEDED(query)) {
        D3D12_DRED_PAGE_FAULT_OUTPUT1 fault{};
        const auto result=dred->GetPageFaultAllocationOutput1(&fault);
        text<<"Page-fault query: 0x"<<(uint32_t)result<<" address: 0x"<<fault.PageFaultVA<<"\n";
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 output{};
        text<<"Breadcrumb query: 0x"<<(uint32_t)dred->GetAutoBreadcrumbsOutput1(&output)<<"\n";
        size_t count=0;
        for(auto node=output.pHeadAutoBreadcrumbNode;node && count++<128;node=node->pNext) {
            const auto complete=node->pLastBreadcrumbValue?*node->pLastBreadcrumbValue:0;
            text<<"list="<<node->pCommandList<<" queue="<<node->pCommandQueue<<std::dec<<" complete="<<complete<<" total="<<node->BreadcrumbCount;
            if(node->pCommandListDebugNameA)text<<" name="<<std::string(node->pCommandListDebugNameA,strnlen_s(node->pCommandListDebugNameA,256));
            text<<" ops=";
            if(node->pCommandHistory)for(UINT i=complete>2?complete-2:0;i<node->BreadcrumbCount && i<complete+8;++i)text<<i<<':'<<(int)node->pCommandHistory[i]<<' ';
            text<<"\n";
        }
    }
    write_file(root()/L"reframework/data/re4unified"/(L"gpu_removed_"+std::to_wstring(GetCurrentProcessId())+L".log"),text.str());
}
}
