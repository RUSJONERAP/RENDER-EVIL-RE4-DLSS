// Validate the consumer's ABI against the actual NVIDIA SDK declaration.
// The SDK is used for this test only and is not copied into the release.
#include <nvsdk_ngx.h>
#include "UnifiedNgxView.hpp"
#include "ActualOptiBridge.hpp"
#include <cstring>
#include <iostream>
struct PublicParameterMock : NVSDK_NGX_Parameter {
#define WRITE(T) void Set(const char*,T) override {}
    WRITE(unsigned long long) WRITE(float) WRITE(double) WRITE(unsigned int) WRITE(int)
    WRITE(ID3D11Resource*) WRITE(ID3D12Resource*) WRITE(void*)
#undef WRITE
#define READ(T) NVSDK_NGX_Result Get(const char*,T*)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
    READ(unsigned long long) READ(float) READ(double) READ(int) READ(ID3D11Resource*) READ(ID3D12Resource*) READ(void*)
#undef READ
    NVSDK_NGX_Result Get(const char* key,unsigned int* out) const override {
        if(std::strcmp(key,"OutWidth")==0){*out=2560;return NVSDK_NGX_Result_Success;}
        if(std::strcmp(key,"OutHeight")==0){*out=1440;return NVSDK_NGX_Result_Success;}
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    void Reset() override {}
};
static int32_t enabled=1,observed=-1;
static int calls=0;
static int32_t get_bool(const char*,int32_t* value){*value=enabled;return 1;}
static int32_t set_bool(const char*,int32_t value){enabled=value;return 1;}
static uint32_t evaluate(ID3D12GraphicsCommandList*,const void*,void*,void*){observed=enabled;++calls;return 1;}
int main(){
    PublicParameterMock actual;auto* view=reinterpret_cast<re4unified::NgxParametersView*>(&actual);
    unsigned int width=0,height=0;
    if(view->read("OutWidth",&width)!=1 || view->read("OutHeight",&height)!=1 || width!=2560 || height!=1440)return 1;
    using namespace re4unified;
    if(!nr_primary_view({2560,1440},{2560,1440}) || nr_primary_view({1024,1024},{2560,1440}) || !nr_primary_view({0,0},{2560,1440}))return 2;
    nr_api::api.module=interop::provider=(HMODULE)1;nr_api::api.ready=true;
    nr_api::api.get_bool=&get_bool;nr_api::api.set_bool=&set_bool;interop::evaluate_provider=&evaluate;
    interop::primary_width.store(2560);interop::primary_height.store(1440);
    interop::feature_views[41]={1024,1024};interop::feature_views[42]={2560,1440};
    uint32_t id=41;interop::evaluate_feature(nullptr,&id,nullptr,nullptr);
    if(observed!=0 || enabled!=1 || calls!=1)return 3;
    id=42;interop::evaluate_feature(nullptr,&id,nullptr,nullptr);
    if(observed!=1 || enabled!=1 || calls!=2)return 4;
    enabled=0;id=41;interop::evaluate_feature(nullptr,&id,nullptr,nullptr);
    if(observed!=0 || enabled!=0 || calls!=3)return 5;
    std::cout<<"PASS public NGX SDK ABI and primary/auxiliary view classification; no game DLL loaded\n";
}
