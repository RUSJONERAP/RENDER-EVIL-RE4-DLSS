#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace re4unified {
inline std::filesystem::path root() {
    wchar_t name[32768]{};const auto count=GetModuleFileNameW(nullptr,name,32768);
    return count && count<32768?std::filesystem::path{name}.parent_path():std::filesystem::path{};
}
inline bool active() {
#if defined(RE4) && defined(RE4_UNIFIED_BUILD)
    // Dedicated distribution build: never fall back into the old PD/research
    // route when somebody copies only the DLL without its data directory.
    return true;
#elif defined(RE4)
    static const bool enabled=std::filesystem::exists(root()/L"reframework/data/re4unified/enabled.txt");return enabled;
#else
    return false;
#endif
}
inline std::string read_file(const std::filesystem::path& path,size_t maximum=2*1024*1024) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)return {};
    const auto size=input.tellg();if(size<0 || (uint64_t)size>maximum)return {};
    std::string data((size_t)size,'\0');input.seekg(0);input.read(data.data(),data.size());return input?data:std::string{};
}
inline bool write_file(const std::filesystem::path& path,const std::string& text) {
    try {
        std::filesystem::create_directories(path.parent_path());
        const auto temp=std::filesystem::path{path.wstring()+L".re4u."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L".tmp"};
        {std::ofstream output(temp,std::ios::binary|std::ios::trunc);if(!output)return false;output.write(text.data(),text.size());output.flush();if(!output)return false;}
        if(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
        std::error_code ignored;std::filesystem::remove(temp,ignored);return false;
    } catch(...) {return false;}
}
}
