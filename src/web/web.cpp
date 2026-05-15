#include "web.h"

#include <sstream>

Web::Web(WebTelemetryHelper& helper) : helper_(helper) {
    RegisterDefaultRoutes();
}

void Web::RegisterRoute(const std::string& path, RouteHandler handler) { routes_[path] = std::move(handler); }

void Web::RegisterDefaultRoutes() {
    RegisterRoute("/", [this] { return WebResponse{200, "text/html; charset=utf-8", GetIndexHtml()}; });
    RegisterRoute("/style.css", [this] { return WebResponse{200, "text/css; charset=utf-8", GetStyleCss()}; });
    RegisterRoute("/app.js", [this] { return WebResponse{200, "application/javascript; charset=utf-8", GetAppJs()}; });
    RegisterRoute("/api/current", [this] { return WebResponse{200, "application/json", GetCurrentSnapshotJson()}; });
    RegisterRoute("/api/history/live", [this] { return WebResponse{200, "application/json", GetLiveHistoryJson()}; });
    RegisterRoute("/api/history/24h", [this] { return WebResponse{200, "application/json", Get24HoursHistoryJson()}; });
    RegisterRoute("/api/history/long", [this] { return WebResponse{200, "application/json", GetLongHistoryJson()}; });
    RegisterRoute("/api/history/sessions", [this] { return WebResponse{200, "application/json", GetSessionHistoryJson()}; });
}

WebResponse Web::HandleRequest(const std::string& path) const {
    auto it = routes_.find(path);
    if (it == routes_.end()) return WebResponse{404, "application/json", "{\"error\":\"not found\"}"};
    return it->second();
}

std::string Web::GetIndexHtml() const { return "<html><head><link rel='stylesheet' href='/style.css'></head><body><button id='burger'>☰</button><aside id='menu' class='menu'><h3>Снапшоты</h3><ul><li>CPU</li><li>GPU</li><li>NET</li><li>RAM</li><li>DISK</li></ul><label><input id='liveToggle' type='checkbox' checked> Live graph</label><label><input id='h24Toggle' type='checkbox' checked> 24h graph</label><label><input id='longToggle' type='checkbox' checked> Long graph</label></aside><main><div id='liveChart' class='chart'></div><div id='h24Chart' class='chart'></div><div id='longChart' class='chart'></div></main><script src='https://cdn.jsdelivr.net/npm/echarts@5/dist/echarts.min.js'></script><script src='/app.js'></script></body></html>"; }
std::string Web::GetStyleCss() const { return "body{margin:0;background:#111827;color:#e5e7eb;font-family:Arial}#burger{position:fixed;left:10px;top:10px;z-index:5}.menu{position:fixed;left:-260px;top:0;width:240px;height:100vh;background:#1f2937;padding:20px;transition:.2s}.menu.open{left:0}.chart{height:280px;margin:18px 20px;background:#1f2937;border-radius:12px}"; }
std::string Web::GetAppJs() const { return R"JS(const menu=document.getElementById('menu');document.getElementById('burger').onclick=()=>menu.classList.toggle('open');const liveChart=echarts.init(document.getElementById('liveChart'));const h24Chart=echarts.init(document.getElementById('h24Chart'));const longChart=echarts.init(document.getElementById('longChart'));const pct=v=>Math.max(0,Math.min(100,v));const time=v=>new Date(v).toLocaleTimeString();
async function j(u){return (await fetch(u)).json();}
function draw(c,x,series){c.setOption({tooltip:{trigger:'axis'},legend:{},xAxis:{type:'category',data:x},yAxis:{type:'value',min:0,max:100},series});}
async function refreshLive(){const d=await j('/api/history/live');draw(liveChart,d.map(r=>time(r.timestampMs)),[{name:'CPU',type:'line',data:d.map(r=>pct(r.cpu.totalUsage))},{name:'GPU',type:'line',data:d.map(r=>pct((r.gpu.gpus[0]||{usagePercent:0}).usagePercent))},{name:'RAM',type:'line',data:d.map(r=>pct((r.memory.usedRAM*100)/Math.max(1,r.memory.totalRAM)))}]);}
function aggToSeries(d){return {x:d.map(r=>time(r.windowEndMs)),cpu:d.map(r=>pct(r.cpuAvg)),gpu:d.map(r=>pct(r.gpuAvg)),ram:d.map(r=>pct(r.ramPercentAvg)),disk:d.map(r=>pct(r.diskPercentAvg))};}
async function refreshHistory(){const h=aggToSeries(await j('/api/history/24h'));draw(h24Chart,h.x,[{name:'CPU',type:'line',data:h.cpu},{name:'GPU',type:'line',data:h.gpu},{name:'RAM',type:'line',data:h.ram},{name:'DISK',type:'line',data:h.disk}]);const l=aggToSeries(await j('/api/history/long'));draw(longChart,l.x,[{name:'CPU',type:'line',data:l.cpu},{name:'GPU',type:'line',data:l.gpu},{name:'RAM',type:'line',data:l.ram},{name:'DISK',type:'line',data:l.disk}]);}
function bindToggle(id,el){document.getElementById(id).onchange=(e)=>{el.style.display=e.target.checked?'block':'none';};}
bindToggle('liveToggle',document.getElementById('liveChart'));bindToggle('h24Toggle',document.getElementById('h24Chart'));bindToggle('longToggle',document.getElementById('longChart'));
setInterval(refreshLive,1000);setInterval(refreshHistory,1000);refreshLive();refreshHistory();
)JS"; }

std::string Web::GetCurrentSnapshotJson() const { return BuildSnapshotJson(helper_.GetCurrentSnapshot()); }
std::string Web::Get24HoursHistoryJson() const { return BuildAggregatedSeriesJson(helper_.Get24HoursSeries()); }
std::string Web::GetSessionHistoryJson() const { return BuildSessionHistoryJson(helper_.GetSessionHistory()); }
std::string Web::GetLiveHistoryJson() const { return BuildLiveSeriesJson(helper_.GetLiveGraphBootstrap()); }
std::string Web::GetLongHistoryJson() const { return BuildAggregatedSeriesJson(helper_.GetLongRangeSeries()); }

std::string Web::EscapeJson(const std::string& value) { std::string out; for (char c: value){ if(c=='"') out+="\\\""; else if(c=='\\') out+="\\\\"; else out+=c;} return out; }
std::string Web::BuildSnapshotJson(const Telemetry::Snapshot& s) { std::ostringstream oss; oss<<"{"<<"\"timestampMs\":"<<s.timestampMs<<",\"cpu\":{\"totalUsage\":"<<s.cpu.totalUsage<<"},\"memory\":{\"totalRAM\":"<<s.memory.totalRAM<<",\"usedRAM\":"<<s.memory.usedRAM<<"},\"gpu\":{\"gpus\":["; for(size_t i=0;i<s.gpu.gpus.size();++i){if(i)oss<<",";oss<<"{\"usagePercent\":"<<s.gpu.gpus[i].usagePercent<<"}";} oss<<"]}}"; return oss.str(); }
std::string Web::BuildAggregatedSeriesJson(const std::vector<Telemetry::AggregatedSnapshot>& series){std::ostringstream oss;oss<<"[";for(size_t i=0;i<series.size();++i){auto&e=series[i];if(i)oss<<",";oss<<"{\"recordType\":"<<(int)e.recordType<<",\"windowEndMs\":"<<e.windowEndMs<<",\"cpuAvg\":"<<e.cpuAvg<<",\"gpuAvg\":"<<e.gpuAvg<<",\"ramPercentAvg\":"<<e.ramPercentAvg<<",\"diskPercentAvg\":"<<e.diskPercentAvg<<"}";}oss<<"]";return oss.str();}
std::string Web::BuildLiveSeriesJson(const std::vector<Telemetry::Snapshot>& series){std::ostringstream oss;oss<<"[";for(size_t i=0;i<series.size();++i){if(i)oss<<",";oss<<BuildSnapshotJson(series[i]);}oss<<"]";return oss.str();}
std::string Web::BuildSessionHistoryJson(const std::vector<std::vector<Telemetry::AggregatedSnapshot>>& sessions){std::ostringstream oss;oss<<"[";for(size_t i=0;i<sessions.size();++i){if(i)oss<<",";oss<<BuildAggregatedSeriesJson(sessions[i]);}oss<<"]";return oss.str();}
