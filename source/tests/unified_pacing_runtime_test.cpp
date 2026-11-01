#define RE4
#include "UnifiedPacing.hpp"
#include <iostream>
int main(){
    using namespace re4unified;
    const auto marker=root()/L"reframework/data/re4unified/enabled.txt";
    // Isolated test executable's own data folder, never the game's folder.
    if(std::filesystem::exists(marker)){std::cerr<<"Unexpected existing test marker\n";return 1;}
    if(!write_file(marker,"pacing test"))return 2;
    pacing::enabled.store(true);pacing::target_fps.store(90);
    pacing::before_present(nullptr);const auto start=pacing::counter();
    for(int i=0;i<120;++i)pacing::before_present(nullptr);
    const double elapsed=(double)(pacing::counter()-start)/pacing::frequency();
    const double fps=120/elapsed;
    std::filesystem::remove(marker);
    std::cout<<"Real timer pacing: target 90 FPS, observed "<<fps<<" FPS, P99 "<<pacing::p99_ms<<" ms; no game DLL loaded\n";
    return fps>=84.6 && fps<=95.4?0:3;
}
