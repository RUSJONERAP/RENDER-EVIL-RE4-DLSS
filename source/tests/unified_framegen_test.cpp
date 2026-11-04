#include "UnifiedFrameGen.hpp"
#include "UnifiedNgxView.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
static unsigned checks=0;
static void check(bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);}
int main(){try {
    using namespace re4unified;
    auto fg=framegen::parse_status("DLSS Quality\r\nFrame gen: on, 2x\r\nFrame gen max: 4\r\n");
    check(fg.reported && fg.ready && fg.generating && fg.multiplier==2 && fg.maximum==4,"Active FG with CRLF status");
    fg=framegen::parse_status("Frame gen: ready\nFrame gen max: 4\n");
    check(fg.ready && !fg.generating && fg.multiplier==1,"Ready is not generation");
    fg=framegen::parse_status("Frame gen: supported, up to 4x (test build: frame generation stays off)\n");
    check(fg.reported && !fg.ready && !fg.generating,"Supported diagnostic is not activation");
    for(auto text:{"Frame gen: off","Frame gen: Streamline started, waiting for the game's device","Frame gen: not available - camera",""}) {
        fg=framegen::parse_status(text);check(!fg.ready && !fg.generating,"Unavailable status never reports active");
    }
    fg=framegen::parse_status("Frame gen: on, 6x\nFrame gen max: 6\n");
    check(fg.multiplier==6 && fg.maximum==6 && fg.generating,"Provider-reported 6x parsed without hardcoded GPU guess");
    fg=framegen::parse_status("Frame gen: on, 99x\nFrame gen max: 999\n");
    check(!fg.generating && fg.maximum==1,"Malformed multiplier cannot enable generation");
    framegen::Mailbox mailbox;std::vector<std::string> delivered;
    check(mailbox.push("fgmult=2") && mailbox.push("reflex=1"),"Queue multiple control edits");
    const auto send=[&](const auto& s){delivered.push_back(s);return true;};
    check(mailbox.pump(true,send) && delivered.empty() && mailbox.pending()==2,"Unread command prevents overwrite");
    check(!mailbox.pump(false,[](const auto&){return false;}) && mailbox.pending()==2,"Failed disk write retains command");
    check(mailbox.pump(false,send) && delivered==std::vector<std::string>{"fgmult=2"} && mailbox.pending()==1,"First edit delivered exactly once");
    check(mailbox.pump(false,send) && delivered==std::vector<std::string>{"fgmult=2","reflex=1"} && mailbox.pending()==0,"Next edit delivered in order after consumption");
    check(!mailbox.push("fgmult=2\nreflex=0") && !mailbox.push(""),"Invalid mailbox entries rejected");
    check(nr_primary_view({2560,1440},{2560,1600}),"Letterboxed 16:9 main view keeps NR");
    check(nr_primary_view({2560,1440},{3440,1440}),"Pillarboxed 16:9 main view keeps NR");
    check(!nr_primary_view({1024,1024},{2560,1440}),"Square cutscene texture bypass retained");
    check(!nr_primary_view({1280,720},{2560,1440}),"Smaller 16:9 auxiliary is not promoted");
    check(!nr_primary_view({2560,1024},{2560,1440}),"Full-width arbitrary texture is not promoted");
    check(!nr_primary_view({3840,2160},{2560,1440}),"Oversized view is not fitted main view");
    std::cout<<"PASS "<<checks<<" FG status/mailbox and fitted NR view checks; no game DLL loaded\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
