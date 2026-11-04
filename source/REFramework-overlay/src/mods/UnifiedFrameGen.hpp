#pragma once
#include "UnifiedSettings.hpp"
#include <charconv>
#include <deque>
#include <string>

namespace re4unified::framegen {
// Actual DLSS owns Streamline and its command/status protocol. No second FG backend.
struct Status {
    bool reported=false,ready=false,generating=false;
    int multiplier=1,maximum=1;
    std::string message;
};
inline int positive_int(std::string_view value,int fallback) {
    int result=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    return parsed.ec==std::errc{} && result>=1 && result<=6?result:fallback;
}
inline Status parse_status(std::string_view source) {
    Status result;
    for(size_t offset=0;offset<source.size();) {
        const auto end=source.find('\n',offset);
        const auto line=trim(source.substr(offset,end==source.npos?source.size()-offset:end-offset));
        offset=end==source.npos?source.size():end+1;
        if(line.starts_with("Frame gen max: "))result.maximum=positive_int(std::string_view(line).substr(15),1);
        else if(line.starts_with("Frame gen: ")) {
            result.reported=true;result.message=trim(std::string_view(line).substr(11));
        }
    }
    // "supported", a saved toggle, and a loaded DLL do not prove generation.
    result.ready=result.message.starts_with("ready") || result.message.starts_with("on,");
    if(result.message.starts_with("on, ")) {
        result.multiplier=positive_int(std::string_view(result.message).substr(4),1);
        result.generating=result.multiplier>1;
    }
    return result;
}
class Mailbox {
    std::deque<std::string> waiting;
public:
    bool push(std::string command) {
        if(command.empty() || command.size()>128 || command.find_first_of("\r\n")!=command.npos || waiting.size()>=32)return false;
        waiting.push_back(std::move(command));return true;
    }
    // An unread command is never overwritten by the next control change.
    template<class Send> bool pump(bool occupied,Send&& send) {
        if(occupied || waiting.empty())return true;
        if(!send(waiting.front()))return false;
        waiting.pop_front();return true;
    }
    size_t pending()const{return waiting.size();}
};
inline Mailbox commands;
}
