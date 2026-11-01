#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace re4unified {
// CPU bookkeeping only. A record is eligible only after an explicit successful
// queue signal covering it has completed; elapsed frames never prove GPU safety.
class Retirement {
    struct Record {uint32_t id;uint64_t fence=0;};
    std::vector<Record> records;
public:
    bool defer(uint32_t id) {
        if(std::any_of(records.begin(),records.end(),[&](auto& r){return r.id==id;}))return false;
        records.push_back({id,0});return true;
    }
    std::vector<uint32_t> unsignalled() const {
        std::vector<uint32_t> ids;for(auto& r:records)if(!r.fence)ids.push_back(r.id);return ids;
    }
    void submitted(const std::vector<uint32_t>& ids,uint64_t fence) {
        if(!fence)return;
        for(auto& r:records)if(!r.fence && std::find(ids.begin(),ids.end(),r.id)!=ids.end())r.fence=fence;
    }
    std::vector<uint32_t> completed(uint64_t fence) {
        std::vector<uint32_t> ids;
        // UINT64_MAX is GetCompletedValue's device-removal sentinel, not success.
        if(fence==UINT64_MAX)return ids;
        for(auto& r:records)if(r.fence && r.fence<=fence)ids.push_back(r.id);
        std::erase_if(records,[&](auto& r){return r.fence && r.fence<=fence;});return ids;
    }
    size_t size() const {return records.size();}
};
}
