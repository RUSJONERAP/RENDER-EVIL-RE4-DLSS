-- Quality selection uses the same public engine API as Actual DLSS's own UI.
-- The original plugin binary remains unchanged. No SR/NR calls or HUD bypass here.
if reframework:get_game_name():lower() ~= "re4" then return end
local last=0
local applied_revision=0
do
    local ok,previous=pcall(json.load_file,"re4unified/engine_status.json")
    if ok and type(previous)=="table" and type(previous.request_revision)=="number" then
        applied_revision=previous.request_revision
    end
end
local function call(t,name,...)
    if not t then return nil end
    local method=t:get_method(name)
    if not method then return nil end
    local ok,result=pcall(method.call,method,nil,...)
    if ok then return result end
end
re.on_frame(function()
    local now=os.clock()
    if now-last<0.5 then return end
    last=now
    local ok,command=pcall(json.load_file,"re4unified/engine_command.json")
    if not ok or type(command)~="table" or type(command.quality)~="number" then return end
    local quality=math.floor(command.quality)
    if quality<0 or quality>4 then return end
    local t=sdk.find_type_definition("via.render.UpscalingInterface")
    local algorithm=call(t,"getUpscalingAlgorithm")
    local revision=type(command.revision)=="number" and command.revision or 0
    if command.requested==true and revision>0 and revision~=applied_revision and algorithm==4 then
        if call(t,"get_UpscalingQuality")~=quality then
            call(t,"set_UpscalingQuality",quality)
            log.info("[RE:NDER EVIL] Explicit quality request: "..tostring(quality)..", revision "..tostring(revision))
        end
        if call(t,"get_UpscalingQuality")==quality then applied_revision=revision end
    end
    local snapshot={algorithm=algorithm,quality=call(t,"get_UpscalingQuality"),requested=quality,request_revision=applied_revision}
    pcall(json.dump_file,"re4unified/engine_status.json",snapshot)
end)
log.info("[RE:NDER EVIL] Native FSR2 quality control active; Actual DLSS owns scene upscaling.")
