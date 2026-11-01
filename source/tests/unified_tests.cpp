#include "UnifiedSettings.hpp"
#include "ActualOptiBridge.hpp"
#include "UnifiedPacing.hpp"
#include <iostream>
#include <vector>
#include <stdexcept>
static int count=0;
static void check(bool value,const char* message){++count;if(!value)throw std::runtime_error(message);}
int main(){try {
    using namespace re4unified;
    const std::string ini="; keep this\r\n[General]\r\nEnabled = 1\r\nPreset = 0\r\n[DlssNr]\r\nEnabled = true\r\nPreset = 2 ; note\r\nIntensity=0.995803\r\n[Other]\r\nEnabled=false\r\n";
    check(ini_get(ini,"DlssNr","Preset")=="2","Read NR preset without inline comment");
    check(ini_get(ini,"general","enabled")=="1","INI keys case-insensitive");
    auto edited=ini_set(ini,"DlssNr","Enabled","false");
    check(ini_get(edited,"DlssNr","Enabled")=="false","NR toggled");
    check(ini_get(edited,"General","Enabled")=="1","NR edit preserves SR enable");
    check(ini_get(edited,"Other","Enabled")=="false","NR edit preserves unrelated section");
    check(edited.starts_with("; keep this\r\n"),"Comments and CRLF preserved");
    edited=ini_set(ini,"DlssNr","LiveReload","true");
    check(ini_get(edited,"DlssNr","LiveReload")=="true","New NR key inserted in correct section");
    check(edited.find("LiveReload = true\r\n[Other]")!=std::string::npos,"Inserted key does not drift into next section");
    edited=ini_set("[Other]\nX=1", "General","Enabled","1");
    check(ini_get(edited,"General","Enabled")=="1" && ini_get(edited,"Other","X")=="1","Missing section appended without modifying existing fields");
    edited=ini_set("[DlssNr]\nEnabled=true\nEnabled=false\n", "DlssNr","Enabled","true");
    check(edited=="[DlssNr]\nEnabled = true\n","Duplicate target keys collapse to one");
    check(ini_set(ini,"DlssNr","Enabled","false\n[Other]")==ini,"Newline injection rejected");
    check(interop::ngx_core_name(L"C:\\Windows\\System32\\_nvngx.dll"),"Driver NGX core recognized");
    check(interop::ngx_core_name(L"C:/Driver/NVNGX.DLL"),"Forward slash and case supported");
    check(!interop::ngx_core_name(L"nvngx_dlss.dll"),"SR runtime is never redirected as NGX core");
    check(!interop::ngx_core_name(L"nvngx.dll_dlssnr.dll"),"NR forwarder is never redirected");
    check(!interop::ngx_core_name(L"d3dcompiler_47.dll"),"Other plugin dependencies unaffected");
    std::vector<std::byte> image(8192);
    auto dos=(IMAGE_DOS_HEADER*)image.data();dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=128;
    auto nt=(IMAGE_NT_HEADERS64*)(image.data()+128);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt->OptionalHeader.SizeOfImage=(DWORD)image.size();
    auto& imports=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];imports.VirtualAddress=0x400;imports.Size=40;
    auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(image.data()+0x400);desc->Name=0x600;desc->OriginalFirstThunk=0x700;desc->FirstThunk=0x800;
    std::memcpy(image.data()+0x600,"KERNEL32.dll",13);
    auto names=(IMAGE_THUNK_DATA64*)(image.data()+0x700);names[0].u1.AddressOfData=0x900;
    auto named=(IMAGE_IMPORT_BY_NAME*)(image.data()+0x900);std::memcpy(named->Name,"LoadLibraryExW",sizeof("LoadLibraryExW"));
    check(interop::find_loader_slot((HMODULE)image.data())==(void**)(image.data()+0x800),"Resolve named loader IAT slot in isolated PE fixture");
    std::memcpy(image.data()+0x600,"OTHERXYZ.dll",13);
    check(!interop::find_loader_slot((HMODULE)image.data()),"Same import name from unrelated DLL rejected");
    std::memcpy(image.data()+0x600,"KERNEL32.dll",13);
    desc->Name=8191;image[8191]=std::byte{65};
    check(!interop::find_loader_slot((HMODULE)image.data()),"Unterminated import DLL name rejected");desc->Name=0x600;
    names[1].u1.AddressOfData=0x900;check(!interop::find_loader_slot((HMODULE)image.data()),"Ambiguous duplicate import rejected");names[1].u1.AddressOfData=0;
    names[0].u1.AddressOfData=0x3000;check(!interop::find_loader_slot((HMODULE)image.data()),"Out-of-image import name rejected");
    names[0].u1.AddressOfData=IMAGE_ORDINAL_FLAG64|5;check(!interop::find_loader_slot((HMODULE)image.data()),"Ordinal import not mistaken for loader");
    names[0].u1.AddressOfData=0x900;names[1].u1.AddressOfData=0xa00;
    auto proc=(IMAGE_IMPORT_BY_NAME*)(image.data()+0xa00);std::memcpy(proc->Name,"GetProcAddress",sizeof("GetProcAddress"));
    check(interop::find_loader_slot((HMODULE)image.data(),"GetProcAddress")==((void**)image.data()+0x808/sizeof(void*)),"Resolve procedure import separately from loader");
    Retirement retired;check(retired.defer(71),"New handle retired");check(!retired.defer(71),"Duplicate retirement not released twice");
    check(retired.completed(999).empty(),"Unsubmitted resource never released, regardless of completion value");
    const auto batch=retired.unsignalled();retired.defer(72);retired.submitted(batch,3);
    check(retired.completed(2).empty(),"Incomplete fence retains resources");
    check(retired.completed(UINT64_MAX).empty(),"Device-removal sentinel never proves completion");
    check(retired.completed(3)==std::vector<uint32_t>{71},"Only covered completed handle released");
    check(retired.size()==1 && retired.unsignalled()==std::vector<uint32_t>{72},"A later request waits for its own submission fence");
    retired.submitted(retired.unsignalled(),0);check(retired.completed(999).empty(),"Failed queue signal does not release");
    retired.submitted(retired.unsignalled(),4);check(retired.completed(4)==std::vector<uint32_t>{72},"Later successful submission retires pending handle");
    pacing::Schedule schedule;
    check(schedule.deadline(100,1000,100)==100,"Cap starts without delaying the first frame");
    check(schedule.deadline(103,1000,100)==110,"Early frame waits for its scheduled deadline");
    check(schedule.deadline(114,1000,100)==120,"Stable cadence maintained");
    check(schedule.deadline(150,1000,100)==150,"Slow frame gets no extra delay or catch-up burst");
    check(schedule.deadline(151,1000,0)==151,"Disabled limiter never delays");
    check(schedule.deadline(152,1000,400)==152,"Out-of-range cap rejected");
    std::cout<<"PASS "<<count<<" unified settings / isolated IAT checks; no game DLL executed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
