#pragma once
#include <cstdint>
struct ID3D11Resource;
struct ID3D12Resource;
namespace re4unified {
// Independent declaration of the public NGX parameter protocol. No SDK code or
// NVIDIA runtime is embedded. Both ends here use the Windows x64 MSVC ABI.
struct NgxParametersView {
    virtual void assign(const char*,unsigned long long)=0;
    virtual void assign(const char*,float)=0;
    virtual void assign(const char*,double)=0;
    virtual void assign(const char*,unsigned int)=0;
    virtual void assign(const char*,int)=0;
    virtual void assign(const char*,ID3D11Resource*)=0;
    virtual void assign(const char*,ID3D12Resource*)=0;
    virtual void assign(const char*,void*)=0;
    virtual uint32_t read(const char*,unsigned long long*)const=0;
    virtual uint32_t read(const char*,float*)const=0;
    virtual uint32_t read(const char*,double*)const=0;
    virtual uint32_t read(const char*,unsigned int*)const=0;
    virtual uint32_t read(const char*,int*)const=0;
    virtual uint32_t read(const char*,ID3D11Resource**)const=0;
    virtual uint32_t read(const char*,ID3D12Resource**)const=0;
    virtual uint32_t read(const char*,void**)const=0;
    virtual void clear()=0;
};
struct Extent {uint32_t width=0,height=0;};
inline bool nr_primary_view(Extent view,Extent primary) {
    // Unknown dimensions stay on the normal path instead of guessing a crop.
    if(!view.width || !view.height || !primary.width || !primary.height ||
       (view.width==primary.width && view.height==primary.height))return true;
    // Actual DLSS 0.3.1 also handles a 16:9 scene fitted inside the swapchain.
    // Accept that full-axis scene, never arbitrary smaller auxiliary textures.
    if(view.width>primary.width || view.height>primary.height)return false;
    if(view.width!=primary.width && view.height!=primary.height)return false;
    const auto wide=uint64_t(view.width)*9,tall=uint64_t(view.height)*16;
    return wide>tall?wide-tall<=32:tall-wide<=32;
}
}
