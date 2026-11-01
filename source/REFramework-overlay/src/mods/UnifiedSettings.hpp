#pragma once
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace re4unified {
inline std::string trim(std::string_view text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==text.npos) return {};
    return std::string{text.substr(first,text.find_last_not_of(" \t\r\n")-first+1)};
}
inline std::string lower(std::string text) {
    for(auto& c:text)c=(char)std::tolower((unsigned char)c);return text;
}
inline std::optional<std::string> ini_get(std::string_view source,std::string section,std::string key) {
    section=lower(section);key=lower(key);std::string current;
    for(size_t offset=0;offset<source.size();) {
        const auto end=source.find('\n',offset);auto line=trim(source.substr(offset,end==source.npos?source.size()-offset:end-offset));
        offset=end==source.npos?source.size():end+1;
        if(line.empty() || line[0]==';' || line[0]=='#')continue;
        if(line[0]=='[') {auto close=line.find(']');if(close!=line.npos)current=lower(trim(std::string_view(line).substr(1,close-1)));continue;}
        const auto equal=line.find('=');
        if(current==section && equal!=line.npos && lower(trim(std::string_view(line).substr(0,equal)))==key) {
            auto value=std::string_view(line).substr(equal+1);const auto comment=value.find(';');
            return trim(value.substr(0,comment));
        }
    }
    return {};
}
inline std::string ini_set(std::string_view source,const std::string& section,const std::string& key,const std::string& value) {
    if(section.find_first_of("\r\n[]")!=section.npos || key.find_first_of("\r\n=[]")!=key.npos || value.find_first_of("\r\n")!=value.npos)
        return std::string(source);
    const std::string newline=source.find("\r\n")!=source.npos?"\r\n":"\n";
    const auto wanted=lower(section),wanted_key=lower(key);
    std::string result,current;bool found=false,in_section=false,inserted=false;
    for(size_t offset=0;offset<source.size();) {
        const auto end=source.find('\n',offset);const auto count=end==source.npos?source.size()-offset:end-offset+1;
        const auto raw=source.substr(offset,count);const auto line=trim(raw);offset+=count;
        if(!line.empty() && line[0]=='[') {
            if(in_section && !inserted) {if(!result.empty() && result.back()!='\n')result+=newline;result+=key+" = "+value+newline;inserted=true;}
            const auto close=line.find(']');current=close==line.npos?"":lower(trim(std::string_view(line).substr(1,close-1)));
            in_section=current==wanted;found|=in_section;
        }
        const auto equal=line.find('=');
        if(in_section && !line.empty() && line[0]!=';' && line[0]!='#' && equal!=line.npos && lower(trim(std::string_view(line).substr(0,equal)))==wanted_key) {
            if(!inserted)result+=key+" = "+value+newline;
            inserted=true; // Remove duplicate copies of this key within the section.
        } else result.append(raw);
    }
    if(!inserted) {
        if(!result.empty() && result.back()!='\n')result+=newline;
        if(!found)result+="["+section+"]"+newline;
        result+=key+" = "+value+newline;
    }
    return result;
}
}
