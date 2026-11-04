#pragma once
#include "UnifiedRuntime.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace re4unified::pacing {
class Schedule {
    int64_t next=0,period=0;
public:
    void reset(){next=period=0;}
    int64_t deadline(int64_t now,int64_t frequency,double fps) {
        if(!std::isfinite(fps) || fps<10 || fps>360 || frequency<=0){reset();return now;}
        const auto interval=(int64_t)std::llround((double)frequency/fps);
        if(interval!=period || !next || now>next+frequency/2) {
            period=interval;next=now+interval;return now;
        }
        const auto target=(std::max)(now,next);next=target+interval;return target;
    }
};
inline std::atomic<bool> enabled{false};
inline std::atomic<int> target_fps{72};
inline std::array<float,240> frame_ms{};
inline size_t cursor=0,samples=0;
inline float measured_fps=0,mean_ms=0,p99_ms=0;
inline int64_t counter(){LARGE_INTEGER n{};QueryPerformanceCounter(&n);return n.QuadPart;}
inline int64_t frequency(){static const auto value=[](){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return f.QuadPart;}();return value;}
inline void before_present(HWND window,bool test_only=false,bool external_pacing=false) {
    if(!active() || test_only)return;
    struct ThreadState {
        Schedule schedule;
        int64_t previous=0,stats_at=0;
        HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_MODIFY_STATE|SYNCHRONIZE);
        ThreadState(){if(!timer)timer=CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_MODIFY_STATE|SYNCHRONIZE);}
        ~ThreadState(){if(timer)CloseHandle(timer);}
    };
    thread_local ThreadState state;
    const auto f=frequency();auto now=counter();
    if(window && (IsIconic(window) || !IsWindowVisible(window) || GetForegroundWindow()!=window)) {
        state.schedule.reset();state.previous=0;return;
    }
    const auto target=state.schedule.deadline(now,f,enabled.load() && !external_pacing?target_fps.load():0);
    // A per-thread high-resolution timer, no global timer-resolution change.
    // Only the final <=200 us use a short spin to absorb scheduler wake error.
    const auto spin_guard=std::max<int64_t>(1,f/5000);
    while(now<target) {
        const auto remaining=target-now;
        if(state.timer && remaining>spin_guard) {
            LARGE_INTEGER due{};due.QuadPart=-std::max<int64_t>(1,(remaining-spin_guard)*10000000/f);
            if(SetWaitableTimerEx(state.timer,&due,0,nullptr,nullptr,nullptr,0))WaitForSingleObject(state.timer,100);
            else Sleep(1);
        }else if(!state.timer && remaining>f/500){Sleep(1);}
        else YieldProcessor();
        now=counter();
    }
    if(state.previous && now>state.previous) {
        const auto ms=(float)((now-state.previous)*1000.0/f);
        frame_ms[cursor]=ms;cursor=(cursor+1)%frame_ms.size();samples=(std::min)(samples+1,frame_ms.size());
        if(now-state.stats_at>f/2) {
            auto sorted=frame_ms;std::sort(sorted.begin(),sorted.begin()+samples);
            double sum=0;for(size_t i=0;i<samples;++i)sum+=sorted[i];
            mean_ms=(float)(sum/samples);measured_fps=mean_ms>0?1000.0f/mean_ms:0;
            p99_ms=sorted[(samples-1)*99/100];state.stats_at=now;
        }
    }
    state.previous=now;
}
}
