#pragma once
#include "UnifiedRuntime.hpp"
#include "UnifiedSettings.hpp"
#include "ActualOptiBridge.hpp"
#include "UnifiedNrApi.hpp"
#include "UnifiedPacing.hpp"
#include <imgui.h>
#include <json.hpp>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>

// Design tokens adapted from DLSS5-Autopilot core/ui/theme.py, MIT.
// Copyright (c) 2026 DLSS 5 Autopilot contributors. See bundled attribution/license.
namespace re4unified {
inline bool expert=false;
inline ImFont* panel_font=nullptr;
inline const char* t(const char* ru,const char* en);
struct PanelState {
    bool initialized=false,russian=true,sr=true,nr=true,before_sr=false,nr_running=false,live_fresh=false;
    int tab=0,quality=1,preset=0,nr_preset=2,passes=1;
    bool limit_enabled=false;
    int fps_limit=72;
    float sharpness=0.15f,intensity=1.0f,work_scale=1.0f,fps=0.0f,nr_ms=0.0f;
    uint64_t refreshed=0,requested=0,sharpen_sent=0;
    std::string actual_status,error,notice,nr_failure;
};
inline PanelState panel;
inline const char* t(const char* ru,const char* en) {return panel.russian?ru:en;}
inline std::filesystem::path data() {return root()/L"reframework/data/re4unified";}
inline std::filesystem::path actual_data() {return root()/L"reframework/data/re4dlss";}
inline int int_value(const std::string& source,const char* section,const char* key,int fallback) {
    try {auto v=ini_get(source,section,key);return v?std::stoi(*v):fallback;}catch(...){return fallback;}
}
inline float float_value(const std::string& source,const char* section,const char* key,float fallback) {
    try {auto v=ini_get(source,section,key);const auto x=v?std::stof(*v):fallback;return std::isfinite(x)?x:fallback;}catch(...){return fallback;}
}
inline bool bool_value(const std::string& source,const char* section,const char* key,bool fallback) {
    const auto v=ini_get(source,section,key);if(!v)return fallback;
    const auto s=lower(*v);return s=="true" || s=="1"?true:(s=="false" || s=="0"?false:fallback);
}
inline std::string decimal(float x) {std::ostringstream out;out<<std::fixed<<std::setprecision(3)<<x;return out.str();}
inline void save_panel(bool update_quality=false) {
    nlohmann::json settings{{"language",panel.russian?"ru":"en"},{"quality",panel.quality},{"fpsLimitEnabled",panel.limit_enabled},{"fpsLimit",panel.fps_limit}};
    pacing::enabled.store(panel.limit_enabled);pacing::target_fps.store(panel.fps_limit);
    if(!write_file(data()/L"ui.json",settings.dump(2)))panel.error=t("Не удалось сохранить настройки меню.","Could not save panel settings.");
    if(update_quality) {
        nlohmann::json engine{{"quality",panel.quality},{"requested",true},{"revision",GetTickCount64()}};
        if(!write_file(data()/L"engine_command.json",engine.dump()))panel.error=t("Не удалось передать режим в игру.","Could not send the quality mode to the game.");
    }
}
inline bool actual_command(const std::string& command) {
    if(write_file(actual_data()/L"command.txt",command))return true;
    panel.error=t("Не удалось передать команду DLSS.","Could not send a DLSS command.");return false;
}
inline bool set_actual(const std::string& section,const std::string& key,const std::string& value) {
    const auto path=actual_data()/L"re4dlss.ini";
    if(!write_file(path,ini_set(read_file(path),section,key,value))) {panel.error=t("Не удалось сохранить параметры SR.","Could not save SR settings.");return false;}
    return actual_command("reload");
}
inline bool set_nr(const std::string& key,const std::string& value) {
    std::lock_guard api_lock(lifetime::api_mutex);
    if(nr_api::api.connect(interop::provider)) {
        int result=0;
        try {
            if(key=="Enabled")result=nr_api::api.set_bool(key.c_str(),value=="true"?1:0);
            else if(key=="Preset" || key=="Passes")result=nr_api::api.set_int(key.c_str(),std::stoi(value));
            else if(key=="Intensity" || key=="WorkingScale")result=nr_api::api.set_float(key.c_str(),std::stof(value));
        }catch(...){panel.error=t("Некорректное значение NR.","Invalid NR setting.");return false;}
        if(result==1) {
            if(nr_api::api.save()!=1){panel.error=t("NR изменён, но сохранить его настройки не удалось.","NR changed, but its settings could not be saved.");return false;}
            panel.notice=t("Параметр NR применён через API.","NR setting applied through the API.");return true;
        }
        if(result<0){panel.error=t("OptiScaler отклонил параметр NR.","OptiScaler rejected the NR setting.");return false;}
        // RunBeforeSR is absent from ABI v1: use the documented LiveReload path.
    }
    const auto path=root()/L"OptiScaler.ini";auto source=read_file(path);
    if(source.empty()) {panel.error=t("OptiScaler.ini недоступен.","OptiScaler.ini is unavailable.");return false;}
    source=ini_set(source,"DlssNr",key,value);source=ini_set(source,"DlssNr","LiveReload","true");
    if(!write_file(path,source)){panel.error=t("Не удалось сохранить параметры NR.","Could not save NR settings.");return false;}
    panel.notice=t("Параметры переданы. Статус ниже обновится из движка.","Settings sent. Runtime status will update below.");return true;
}
inline uint64_t unix_ms() {FILETIME ft{};GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER n{};n.LowPart=ft.dwLowDateTime;n.HighPart=ft.dwHighDateTime;return n.QuadPart/10000-11644473600000ull;}
inline void load_preferences() {
    if(panel.initialized)return;
    try {
        auto settings=nlohmann::json::parse(read_file(data()/L"ui.json"),nullptr,false);
        if(settings.is_object()) {
            panel.russian=settings.value("language",std::string("ru"))!="en";
            panel.quality=std::clamp(settings.value("quality",1),0,4);
            panel.limit_enabled=settings.value("fpsLimitEnabled",false);
            panel.fps_limit=std::clamp(settings.value("fpsLimit",72),10,360);
        }
    }catch(...){}
    pacing::enabled.store(panel.limit_enabled);pacing::target_fps.store(panel.fps_limit);
    panel.initialized=true;
}
inline void refresh() {
    std::lock_guard api_lock(lifetime::api_mutex);
    const auto now=GetTickCount64();
    load_preferences();
    if(now-panel.refreshed<500)return;panel.refreshed=now;
    const auto actual=read_file(actual_data()/L"re4dlss.ini");const auto nr=read_file(root()/L"OptiScaler.ini");
    if(!ImGui::IsAnyItemActive()) {
    panel.sr=bool_value(actual,"General","Enabled",true);panel.preset=int_value(actual,"General","Preset",0);
    panel.sharpness=std::clamp(float_value(actual,"Integrated","Sharpness",0.15f),0.0f,1.0f);
    panel.nr=bool_value(nr,"DlssNr","Enabled",true);panel.nr_preset=std::clamp(int_value(nr,"DlssNr","Preset",2),0,2);
    panel.intensity=std::clamp(float_value(nr,"DlssNr","Intensity",1.0f),0.0f,2.0f);
    panel.work_scale=std::clamp(float_value(nr,"DlssNr","WorkingScale",1.0f),0.25f,1.25f);
    panel.before_sr=bool_value(nr,"DlssNr","RunBeforeSR",false);panel.passes=std::clamp(int_value(nr,"DlssNr","Passes",1),1,3);
    }
    panel.actual_status=read_file(actual_data()/L"status.txt",16384);
    panel.live_fresh=false;panel.nr_running=false;
    auto live=nlohmann::json::parse(read_file(root()/L"OptiScaler.live.json",131072),nullptr,false);
    if(live.is_object() && live.contains("pid") && live["pid"].is_number_unsigned() && live["pid"].get<uint32_t>()==GetCurrentProcessId() && live.contains("at") && live["at"].is_number()) {
        const auto age=(int64_t)unix_ms()-live["at"].get<int64_t>();panel.live_fresh=age>=0 && age<6000;
        if(panel.live_fresh) {
            if(live.contains("fps") && live["fps"].is_number())panel.fps=live["fps"].get<float>();
            if(live.contains("nr") && live["nr"].is_object()) {
                const auto& state=live["nr"];panel.nr_running=state.value("running",false);
                if(state.contains("modelMs") && state["modelMs"].is_number())panel.nr_ms=state["modelMs"].get<float>();
            }
        }
    }
    panel.nr_failure.clear();
    if(nr_api::api.connect(interop::provider)) {
        auto& api=nr_api::api;nr_api::Status state;
        if(api.status(&state)==1) {
            panel.live_fresh=true;panel.nr_running=state.running!=0;
            panel.nr_ms=(float)state.gpuMs;
            if(state.failureReason)panel.nr_failure.assign(state.failureReason,strnlen_s(state.failureReason,2048));
        }
        if(!ImGui::IsAnyItemActive()) {
            int32_t value=0;float scalar=0;
            if(api.get_bool("Enabled",&value)==1)panel.nr=value!=0;
            if(api.get_int("Preset",&value)==1)panel.nr_preset=std::clamp((int)value,0,2);
            if(api.get_int("Passes",&value)==1)panel.passes=std::clamp((int)value,1,3);
            if(api.get_float("Intensity",&scalar)==1 && std::isfinite(scalar))panel.intensity=std::clamp(scalar,0.0f,2.0f);
            if(api.get_float("WorkingScale",&scalar)==1 && std::isfinite(scalar))panel.work_scale=std::clamp(scalar,0.25f,1.25f);
        }
    }
    if(now-panel.requested>=1000){write_file(root()/L"OptiScaler.live.request","1");panel.requested=now;}
}
inline ImVec4 colour(unsigned rgb,float alpha=1.0f) {return {(rgb>>16&255)/255.0f,(rgb>>8&255)/255.0f,(rgb&255)/255.0f,alpha};}
inline void load_panel_font(float size) {
    panel_font=nullptr;if(!active())return;
    wchar_t windows[MAX_PATH]{};if(!GetWindowsDirectoryW(windows,MAX_PATH))return;
    const auto path=std::filesystem::path(windows)/L"Fonts/segoeui.ttf";
    if(std::filesystem::exists(path))panel_font=ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(),size);
}
inline void muted(const char* text) {ImGui::TextColored(colour(0x8c929b),"%s",text);}
inline void heading(const char* title,const char* subtitle) {
    if(panel_font)ImGui::PushFont(panel_font,24);
    ImGui::TextUnformatted(title);if(panel_font)ImGui::PopFont();ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text,colour(0x8c929b));ImGui::TextWrapped("%s",subtitle);ImGui::PopStyleColor();ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
}
inline void card_begin(const char* id) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg,colour(0x14161a));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{18,16});
    ImGui::BeginChild(id,{0,0},ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding);
}
inline void card_end(){ImGui::EndChild();ImGui::PopStyleVar();ImGui::PopStyleColor();}
inline void fsr_requirement() {
    ImGui::TextColored(colour(0xe6ac50),"%s",t("В игре: FSR 2 — ВКЛ. · FSR 1 — ВЫКЛ.","In game: FSR 2 — ON · FSR 1 — OFF"));
    ImGui::TextWrapped("%s",t("Actual DLSS заменяет проход FSR 2 на DLSS. Качество DLSS выбирается в этой панели.","Actual DLSS replaces the FSR 2 pass with DLSS. Select the DLSS quality mode in this panel."));
}
inline void nr_status() {
    const auto good=panel.live_fresh && panel.nr_running;
    ImGui::TextColored(colour(good?0x86d493:0xe6ac50),"%s",good?t("NR выполняется","NR is running"):(panel.nr?t("NR: ожидаем подтверждение","NR: awaiting runtime confirmation"):t("NR выключен","NR is off")));
    if(good && panel.nr_ms>=0)ImGui::Text("NR: %.2f ms",panel.nr_ms);
    if(!panel.nr_failure.empty())ImGui::TextWrapped("%s",panel.nr_failure.c_str());
}
inline void draw_sr() {
    heading(t("DLSS Super Resolution","DLSS Super Resolution"),t("Обработка сцены до HUD и постобработки","Scene processing before HUD and post-processing"));
    card_begin("fsr_requirement_sr");fsr_requirement();card_end();ImGui::Spacing();
    if(ImGui::Checkbox(t("Включить DLSS SR##sr","Enable DLSS SR##sr"),&panel.sr))set_actual("General","Enabled",panel.sr?"1":"0");
    muted(t("При отключении используется штатный FSR 2 для сравнения.","When disabled, the game uses FSR 2 for comparison."));
    const char* names[]{"DLAA · 100%","Quality · 67%","Balanced · 58%","Performance · 50%","Ultra Performance · 33%"};
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo(t("##quality","##quality"),names[panel.quality])) {
        for(int i=0;i<5;++i)if(ImGui::Selectable(names[i],i==panel.quality)){panel.quality=i;save_panel(true);}
        ImGui::EndCombo();
    }
    const int values[]{0,11,10,13,12,5};const char* presets[]{"Auto","K · Transformer","J · Transformer","M · Transformer","L · Transformer","E · CNN"};
    int selected=0;for(int i=0;i<6;++i)if(values[i]==panel.preset)selected=i;
    if(ImGui::BeginCombo(t("Модель SR##preset","SR model##preset"),presets[selected])) {
        for(int i=0;i<6;++i)if(ImGui::Selectable(presets[i],i==selected)){panel.preset=values[i];set_actual("General","Preset",std::to_string(panel.preset));}
        ImGui::EndCombo();
    }
    ImGui::SliderFloat(t("Резкость SR##sharp","SR sharpness##sharp"),&panel.sharpness,0,1,"%.2f");
    if(ImGui::IsItemDeactivatedAfterEdit())actual_command("sharpness="+decimal(panel.sharpness));
    ImGui::Spacing();muted(t("Для RTX 5070 Ti / 1440p: начни с Quality и Auto или K.","RTX 5070 Ti / 1440p: start with Quality and Auto or K."));
    if(ImGui::CollapsingHeader(t("Статус апскейлера","Upscaler status")))ImGui::TextWrapped("%s",panel.actual_status.empty()?t("Ожидаем статус мода.","Waiting for the mod's status."):panel.actual_status.c_str());
}
inline void draw_pacing() {
    heading(t("Плавность","Frame pacing"),t("Произвольный лимит FPS и время кадра","Custom FPS limit and frame-time readings"));
    card_begin("fps_controls");
    if(ImGui::Checkbox(t("Ограничивать FPS##limit","Limit frame rate##limit"),&panel.limit_enabled))save_panel();
    ImGui::SetNextItemWidth(180);
    if(ImGui::InputInt(t("Целевой FPS##fps","Target FPS##fps"),&panel.fps_limit,1,10)) {
        panel.fps_limit=std::clamp(panel.fps_limit,10,360);save_panel();
    }
    const int presets[]{60,72,90,100,120,144,165};
    for(int i=0;i<7;++i) {
        if(i)ImGui::SameLine();char label[12]{};sprintf_s(label,"%d",presets[i]);
        if(ImGui::Button(label,{58,32})){panel.fps_limit=presets[i];panel.limit_enabled=true;save_panel();}
    }
    ImGui::Spacing();ImGui::TextWrapped("%s",t("В игре выберите «Переменная» частота кадров, чтобы штатный лимит не мешал выбранному значению. Ограничитель не повышает FPS ниже цели.","Use Variable frame rate in the game so its built-in cap does not override your target. A cap cannot increase FPS below the target."));
    card_end();ImGui::Spacing();card_begin("fps_readings");
    ImGui::Text("%.1f FPS  |  %.2f ms",pacing::measured_fps,pacing::mean_ms);
    ImGui::Text("P99: %.2f ms",pacing::p99_ms);
    ImGui::PlotLines("##frame_times",pacing::frame_ms.data(),(int)pacing::samples,(int)pacing::cursor,nullptr,0,40,{ImGui::GetContentRegionAvail().x,100});
    ImGui::TextWrapped("%s",t("Для ровной подачи кадров цель должна быть ниже устойчивого FPS сцены. NR остаётся независимым от ограничителя.","For even pacing, choose a target below the scene's sustained FPS. NR remains independent of the limiter."));
    card_end();
}
inline void draw_nr() {
    heading(t("DLSS Neural Rendering","DLSS Neural Rendering"),t("Независимая нейрообработка через OptiScaler","Independent neural processing through OptiScaler"));
    if(ImGui::Checkbox(t("Включить Neural Rendering##nr","Enable Neural Rendering##nr"),&panel.nr))set_nr("Enabled",panel.nr?"true":"false");
    nr_status();ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    const char* models[]{"Auto","A","B"};
    if(ImGui::BeginCombo(t("Модель NR##nrmodel","NR model##nrmodel"),models[panel.nr_preset])) {
        for(int i=0;i<3;++i)if(ImGui::Selectable(models[i],i==panel.nr_preset)){panel.nr_preset=i;set_nr("Preset",std::to_string(i));}
        ImGui::EndCombo();
    }
    ImGui::SliderFloat(t("Интенсивность##intensity","Intensity##intensity"),&panel.intensity,0,2,"%.2f");
    if(ImGui::IsItemDeactivatedAfterEdit())set_nr("Intensity",decimal(panel.intensity));
    const float scales[]{1.0f,0.75f,0.55f};const char* scale_names[]{"100% · Quality","75% · Balanced","55% · Performance"};int scale=0;
    for(int i=0;i<3;++i)if(std::abs(scales[i]-panel.work_scale)<0.01f)scale=i;
    if(ImGui::BeginCombo(t("Размер обработки NR##scale","NR processing size##scale"),scale_names[scale])) {
        for(int i=0;i<3;++i)if(ImGui::Selectable(scale_names[i],i==scale)){panel.work_scale=scales[i];set_nr("WorkingScale",decimal(scales[i]));}
        ImGui::EndCombo();
    }
    if(ImGui::Checkbox(t("NR до масштабирования##pre","NR before upscaling##pre"),&panel.before_sr))set_nr("RunBeforeSR",panel.before_sr?"true":"false");
    muted(t("Выключено: NR после SR — больше деталей и выше нагрузка.","Off: NR after SR — more detail and higher GPU cost."));
    ImGui::SliderInt(t("Проходы##passes","Passes##passes"),&panel.passes,1,3);
    if(ImGui::IsItemDeactivatedAfterEdit())set_nr("Passes",std::to_string(panel.passes));
    ImGui::Spacing();muted(t("Рекомендуется: модель B, 1 проход, 100% для качества.","Recommended: model B, 1 pass, 100% for quality."));
}
inline bool draw() {
    try {refresh();}catch(...) {panel.error=t("Не удалось прочитать статус. Настройки игры сохранены.","Could not read status. Game settings are retained.");}
    const auto previous=ImGui::GetStyle();auto& style=ImGui::GetStyle();
    style.WindowRounding=10;style.ChildRounding=6;style.FrameRounding=4;style.FrameBorderSize=1;
    style.WindowPadding={20,18};style.FramePadding={10,7};style.ItemSpacing={10,12};
    style.Colors[ImGuiCol_WindowBg]=colour(0x0a0b0d);style.Colors[ImGuiCol_ChildBg]=colour(0x0d0e11);
    style.Colors[ImGuiCol_Text]=colour(0xece9e2);style.Colors[ImGuiCol_TextDisabled]=colour(0x8c929b);
    style.Colors[ImGuiCol_Border]=colour(0x23272d);style.Colors[ImGuiCol_Separator]=colour(0x23272d);
    style.Colors[ImGuiCol_TitleBg]=colour(0x0d0e11);style.Colors[ImGuiCol_TitleBgActive]=colour(0x1b1e23);
    style.Colors[ImGuiCol_ResizeGrip]=colour(0xe6ac50,0.25f);style.Colors[ImGuiCol_ResizeGripHovered]=colour(0xe6ac50,0.65f);
    for(auto key:{ImGuiCol_FrameBg,ImGuiCol_Button})style.Colors[key]=colour(0x14161a);
    for(auto key:{ImGuiCol_FrameBgHovered,ImGuiCol_ButtonHovered,ImGuiCol_HeaderHovered})style.Colors[key]=colour(0x23272d);
    for(auto key:{ImGuiCol_ButtonActive,ImGuiCol_Header,ImGuiCol_HeaderActive})style.Colors[key]=colour(0x1b1e23);
    for(auto key:{ImGuiCol_CheckMark,ImGuiCol_SliderGrab,ImGuiCol_SliderGrabActive})style.Colors[key]=colour(0xe6ac50);
    ImGui::SetNextWindowSizeConstraints({940,680},{1600,1100});
    ImGui::SetNextWindowSize({1040,760},ImGuiCond_FirstUseEver);ImGui::SetNextWindowPos({60,60},ImGuiCond_FirstUseEver);
    bool open=true;if(panel_font)ImGui::PushFont(panel_font,0);
    if(ImGui::Begin("RE:NDER EVIL (DLSS SR/NR)###re4_unified",&open,ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(colour(0xe6ac50),"RE:NDER EVIL");ImGui::SameLine();muted("  DLSS SR / NR · Resident Evil 4");
        ImGui::SameLine(ImGui::GetWindowWidth()-170);
        if(ImGui::Button(panel.russian?"RU":"EN",{56,0})){panel.russian=!panel.russian;save_panel();}
        ImGui::SameLine();if(ImGui::Button(t("Закрыть","Close")))open=false;
        ImGui::Separator();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12,16});
        ImGui::BeginChild("rail",{224,0},ImGuiChildFlags_AlwaysUseWindowPadding);
        const char* tabs[]{t("Обзор","Overview"),t("Масштабирование","Super Resolution"),t("Нейрорендеринг","Neural Rendering"),t("Плавность","Frame pacing"),t("Диагностика","Diagnostics"),t("О сборке","About the build")};
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,{0,0.5f});ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{16,10});ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
        for(int i=0;i<6;++i) {
            const bool selected=i==panel.tab;
            ImGui::PushStyleColor(ImGuiCol_Text,colour(selected?0xe6ac50:0x8c929b));
            ImGui::PushStyleColor(ImGuiCol_Button,colour(selected?0x1b1e23:0x0d0e11));
            if(ImGui::Button(tabs[i],{-1,44}))panel.tab=i;
            if(selected){auto p=ImGui::GetItemRectMin();auto q=ImGui::GetItemRectMax();ImGui::GetWindowDrawList()->AddRectFilled({p.x,p.y+10},{p.x+3,q.y-10},ImGui::ColorConvertFloat4ToU32(colour(0xe6ac50)),2);}
            ImGui::PopStyleColor(2);
        }
        ImGui::PopStyleVar(3);ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
        ImGui::SetCursorPosX(28);muted("RUSJONERAP + Codex");ImGui::SetCursorPosX(28);muted("0.1.0 RC.2 · Insert");
        ImGui::EndChild();ImGui::PopStyleVar();ImGui::SameLine();
        ImGui::BeginChild("content",{0,0},false,ImGuiWindowFlags_AlwaysUseWindowPadding);
        if(panel.tab==0) {
            heading(t("Изображение под контролем","Your image, your settings"),t("DLSS Super Resolution и Neural Rendering","DLSS Super Resolution and Neural Rendering"));
            card_begin("overview_sr");
            ImGui::TextUnformatted("Super Resolution");
            // The plugin's raw second line says "FSR 2 off" about the replaced
            // GPU pass. Keep that implementation wording on Diagnostics only;
            // it must not contradict the required in-game FSR 2 setting here.
            const auto sr_line=panel.actual_status.substr(0,panel.actual_status.find('\n'));
            ImGui::TextWrapped("%s",sr_line.empty()?t("Ожидаем подключение Actual DLSS.","Waiting for Actual DLSS."):sr_line.c_str());
            card_end();ImGui::Spacing();card_begin("overview_nr");ImGui::TextUnformatted("Neural Rendering");nr_status();card_end();
            ImGui::Spacing();card_begin("overview_requirements");fsr_requirement();card_end();
        } else if(panel.tab==1)draw_sr();else if(panel.tab==2)draw_nr();else if(panel.tab==3)draw_pacing();
        else if(panel.tab==4) {
            heading(t("Диагностика","Diagnostics"),t("Статус загрузки и сообщения компонентов","Component loading status and messages"));
            ImGui::TextWrapped("Actual DLSS: %s",GetModuleHandleW(L"re4dlss.dll")?t("загружен","loaded"):t("не загружен","not loaded"));
            ImGui::TextWrapped("OptiScaler: %s",interop::provider?t("провайдер найден","provider found"):t("не найден","not found"));
            ImGui::TextWrapped("NGX: %s · %u",interop::status.c_str(),interop::redirects.load());
            ImGui::Text("SR retire: %u pending / %u completed",lifetime::pending.load(),lifetime::released.load());
            ImGui::Text("NR auxiliary views bypassed: %u",interop::auxiliary_skips.load());
            ImGui::TextWrapped("NR: %s",panel.live_fresh?t("свежий статус движка","fresh runtime status"):t("подтверждение отсутствует","no runtime confirmation"));
            ImGui::TextWrapped("NR API: %s",nr_api::api.ready?"v1":t("резервный путь через INI","INI fallback"));
            if(ImGui::Button(t("Перезагрузить настройки SR","Reload SR settings")))actual_command("reload");
            if(ImGui::Button(t("Открыть REFramework","Open REFramework")))expert=true;
            muted("reframework/data/re4dlss/re4dlss.log");muted("OptiScaler.log");
        } else {
            heading(t("О сборке","About the build"),"RE:NDER EVIL (DLSS SR/NR) · 0.1.0 RC.2");
            card_begin("build_credits");
            ImGui::TextColored(colour(0xe6ac50),"RUSJONERAP + Codex");
            ImGui::TextWrapped("%s",t("Авторы сборки, интеграции и единой панели управления.","Build, integration and unified control panel authors."));
            ImGui::Spacing();ImGui::TextWrapped("%s",t("Actual DLSS (No UI Blur) — khanguyen111. Автор мода, который переносит DLSS до отрисовки интерфейса и постобработки.","Actual DLSS (No UI Blur) — khanguyen111. Author of the mod that runs DLSS before UI and post-processing."));
            muted("nexusmods.com/residentevil42023/mods/7795");
            ImGui::Spacing();ImGui::TextWrapped("%s",t("Neural Rendering — OptiScaler F5 и его участники. NVIDIA — технологии и runtime DLSS. REFramework — praydog; основа ветки — TheRazerMD / PureDark.","Neural Rendering — OptiScaler F5 and contributors. NVIDIA — DLSS technologies and runtimes. REFramework — praydog; branch foundation — TheRazerMD / PureDark."));
            muted("DLSS SR 310.9.16.0 · DLSS NR 310.9.16.0");
            ImGui::Spacing();ImGui::TextWrapped("%s",t("Дизайн вдохновлён DLSS5-Autopilot — Kizzuwatnaa и участники. Адаптированы токены оформления; лицензия MIT сохранена.","Design inspired by DLSS5-Autopilot — Kizzuwatnaa and contributors. Visual tokens adapted; MIT license retained."));
            muted("github.com/Kizzuwatnaa/DLSS5-Autopilot");
            card_end();
        }
        if(!panel.notice.empty())ImGui::TextWrapped("%s",panel.notice.c_str());
        if(!panel.error.empty())ImGui::TextColored(colour(0xec6f5f),"%s",panel.error.c_str());
        ImGui::EndChild();
    }
    ImGui::End();if(panel_font)ImGui::PopFont();ImGui::GetStyle()=previous;return open;
}
}
