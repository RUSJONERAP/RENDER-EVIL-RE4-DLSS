local apply,frame,quality,algorithm,writes=0,nil,1,4,0
reframework={get_game_name=function()return "re4"end}
log={info=function()end}
re={on_frame=function(fn)frame=fn end}
local clock=0;os.clock=function()return clock end
local requested={quality=2,requested=true,revision=1}
json={load_file=function()return requested end,dump_file=function()writes=writes+1 end}
local t={get_method=function(_,name)return {call=function(_,_,value)
    if name=="getUpscalingAlgorithm"then return algorithm end
    if name=="get_UpscalingQuality"then return quality end
    if name=="set_UpscalingQuality"then apply=apply+1;quality=value end
end}end}
sdk={find_type_definition=function()return t end}
assert(loadfile("unified_native.lua"))()
clock=1;frame();assert(quality==2 and apply==1)
clock=2;frame();assert(apply==1,"steady state must not reset render resolution")
quality=3;clock=2.5;frame();assert(quality==3,"an old request must not fight a game-driven resolution transition")
requested={quality=0,requested=true,revision=2};clock=3;frame();assert(quality==0,"DLAA passes native quality zero")
algorithm=0;requested={quality=3,requested=true,revision=3};clock=4;frame();assert(quality==0,"must not change non-FSR2 pipeline")
requested={quality=99};clock=5;frame();assert(quality==0,"invalid mode rejected")
requested=nil;clock=6;frame();assert(quality==0,"missing control file contained")
algorithm=4;requested={quality=2,requested=false,revision=4};clock=7;frame();assert(quality==0,"startup/language settings never force a quality transition")
print("PASS: unified native quality, DLAA, steady-state, inactive pipeline and invalid-input checks")
