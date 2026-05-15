#include "web.h"

#include <sstream>

namespace Web {

WebSite::WebSite(WebTelemetryHelper& helper) : helper_(helper) {}

std::string WebSite::GetIndexHtml() const {
    return R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>SystemTraceX Dashboard</title>
  <link rel="stylesheet" href="/style.css" />
  <script src="https://cdn.jsdelivr.net/npm/echarts@5/dist/echarts.min.js"></script>
</head>
<body>
  <main class="layout"><header class="topbar"><h1>SystemTraceX</h1><p>Realtime telemetry (refresh every 1 second)</p></header><section class="cards" id="cards"></section><section class="panel table-toggle-panel"><h2>Visible tables</h2><div id="tableToggles" class="toggles"></div></section><section class="panel tables-panel" id="tablesPanel"></section><section class="panel charts-panel"><div class="charts-header"><h2>History chart</h2><div class="history-mode"><button data-mode="24h" class="active">24 hours</button><button data-mode="session">By session</button></div><select id="sessionSelect" disabled></select></div><div id="historyChart" class="chart"></div></section><section class="panel charts-panel"><div class="charts-header"><h2>Live chart</h2></div><div id="liveChart" class="chart"></div></section></main>
  <script src="/app.js"></script>
</body>
</html>)HTML";
}
std::string WebSite::GetStyleCss() const {
    std::string css = R"CSS(:root { color-scheme: dark; } body { margin: 0; font-family: Inter, Arial, sans-serif; background: #0e1220; color: #ecf0ff; } .layout { padding: 20px; max-width: 1280px; margin: 0 auto; } .topbar { margin-bottom: 16px; } .topbar h1 { margin: 0; } .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 12px; } .card, .panel, .table-wrap { background: #171d31; border: 1px solid #293252; border-radius: 16px; } .card { padding: 12px; } .card .k { color: #9eb0ec; font-size: 13px; } .card .v { font-size: 22px; font-weight: 700; margin-top: 4px; } .panel { padding: 16px; margin-top: 14px; })CSS";
    css += R"CSS(.toggles { display: flex; flex-wrap: wrap; gap: 10px; } .toggles label { display: inline-flex; align-items: center; gap: 6px; background: #202946; border-radius: 999px; padding: 8px 12px; } .tables-panel { display: grid; gap: 12px; } .table-wrap { padding: 12px; } table { width: 100%; border-collapse: collapse; } th, td { text-align: left; padding: 8px; border-bottom: 1px solid #28304d; } .charts-header { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; } .history-mode button { background: #263157; color: #f0f3ff; border: 1px solid #3f5188; border-radius: 8px; padding: 6px 10px; cursor: pointer; } .history-mode button.active { background: #3a4f92; } .chart { width: 100%; height: 420px; margin-top: 10px; })CSS";
    return css;
}
std::string WebSite::GetAppJs() const {
    std::string js = R"JS(const cards = document.getElementById('cards');
const tableToggles = document.getElementById('tableToggles');
const tablesPanel = document.getElementById('tablesPanel');
const sessionSelect = document.getElementById('sessionSelect');
const modeButtons = [...document.querySelectorAll('.history-mode button')];
const chart = echarts.init(document.getElementById('historyChart'));
const liveChart = echarts.init(document.getElementById('liveChart'));
const tableState = new Map([['cpu', true], ['memory', true], ['disk', true], ['network', true], ['system', true], ['gpu', true]]);
let chartMode = '24h'; let sessions = []; let series24h = []; let liveSeries = [];
const fmtMb = v => (v / (1024 * 1024)).toFixed(1); const fmtGb = v => (v / (1024 * 1024 * 1024)).toFixed(2); const fmtTime = ms => new Date(ms).toLocaleTimeString();
async function loadJson(url) { const r = await fetch(url); return r.json(); }
)JS";
    js += R"JS(function drawCards(s){cards.innerHTML=[['CPU',`${s.cpu.totalUsage.toFixed(1)}%`],['RAM used',`${fmtMb(s.memory.usedRAM)} MB`],['Network RX',`${s.network.totalRxPerSec.toFixed(0)} B/s`],['Network TX',`${s.network.totalTxPerSec.toFixed(0)} B/s`],['Disks',String(s.disk.disks.length)],['GPU count',String(s.gpu.gpus.length)]].map(([k,v])=>`<article class="card"><div class="k">${k}</div><div class="v">${v}</div></article>`).join('')}
function table(title,id,html){const hidden=!tableState.get(id);return `<section class="table-wrap" style="display:${hidden?'none':'block'}"><h3>${title}</h3>${html}</section>`}
function drawTables(s){const cpuRows=s.cpu.perCoreUsage.map((v,i)=>`<tr><td>Core ${i}</td><td>${v.toFixed(1)}%</td></tr>`).join('');const diskRows=s.disk.disks.map(d=>`<tr><td>${d.name}</td><td>${fmtGb(d.freeBytes)} GB</td><td>${fmtGb(d.totalBytes)} GB</td></tr>`).join('');const netRows=s.network.interfaces.map(n=>`<tr><td>${n.name}</td><td>${n.ipv4}</td><td>${n.rxBytesPerSec.toFixed(0)}</td><td>${n.txBytesPerSec.toFixed(0)}</td></tr>`).join('');const gpuRows=s.gpu.gpus.map(g=>`<tr><td>${g.name}</td><td>${g.usagePercent.toFixed(1)}%</td><td>${fmtMb(g.vramUsedBytes)} MB</td><td>${fmtMb(g.vramTotalBytes)} MB</td></tr>`).join('');tablesPanel.innerHTML=table('CPU','cpu',`<table><tr><th>Core</th><th>Usage</th></tr>${cpuRows}</table>`)+table('Memory','memory',`<table><tr><th>Used</th><th>Free</th><th>Total</th></tr><tr><td>${fmtMb(s.memory.usedRAM)} MB</td><td>${fmtMb(s.memory.freeRAM)} MB</td><td>${fmtMb(s.memory.totalRAM)} MB</td></tr></table>`)+table('Disk','disk',`<table><tr><th>Name</th><th>Free</th><th>Total</th></tr>${diskRows}</table>`)+table('Network','network',`<table><tr><th>Name</th><th>IPv4</th><th>RX B/s</th><th>TX B/s</th></tr>${netRows}</table>`)+table('System','system',`<table><tr><th>OS</th><th>Kernel</th><th>Host</th><th>Uptime sec</th></tr><tr><td>${s.system.osName}</td><td>${s.system.kernelVersion}</td><td>${s.system.hostname}</td><td>${s.system.uptimeSeconds}</td></tr></table>`)+table('GPU','gpu',`<table><tr><th>Name</th><th>Usage</th><th>VRAM used</th><th>VRAM total</th></tr>${gpuRows}</table>`)}
)JS";
    js += R"JS(function drawToggles(){tableToggles.innerHTML=[...tableState.keys()].map(key=>`<label><input type="checkbox" data-key="${key}" ${tableState.get(key)?'checked':''}/> ${key}</label>`).join('');tableToggles.querySelectorAll('input').forEach(i=>i.onchange=()=>{tableState.set(i.dataset.key,i.checked);refreshSnapshot();});}
function asMetricSeries(rows){const filtered=rows.filter(r=>r.recordType===0);return{x:filtered.map(r=>fmtTime(r.windowEndMs)),cpu:filtered.map(r=>r.cpuAvg),ram:filtered.map(r=>r.ramUsedAvg/(1024*1024))};}
function redrawChart(){const source=chartMode==='24h'?series24h:(sessions[sessionSelect.value]||[]);const p=asMetricSeries(source);chart.setOption({tooltip:{trigger:'axis'},legend:{data:['CPU %','RAM MB']},dataZoom:[{type:'inside'},{type:'slider'}],xAxis:{type:'category',data:p.x},yAxis:[{type:'value',name:'CPU %'},{type:'value',name:'RAM MB'}],series:[{name:'CPU %',type:'line',smooth:true,data:p.cpu},{name:'RAM MB',type:'line',smooth:true,yAxisIndex:1,data:p.ram}]});}
async function refreshSnapshot(){const s=await loadJson('/api/current');drawCards(s);drawTables(s);}
async function initHistory(){series24h=await loadJson('/api/history/24h');sessions=await loadJson('/api/history/sessions');sessionSelect.innerHTML=sessions.map((_,i)=>`<option value="${i}">Session ${i+1}</option>`).join('');redrawChart();}
function redrawLiveChart(){const x=liveSeries.map(r=>fmtTime(r.timestampMs));const cpu=liveSeries.map(r=>r.cpu.totalUsage);const ram=liveSeries.map(r=>r.memory.usedRAM/(1024*1024));liveChart.setOption({tooltip:{trigger:'axis'},legend:{data:['CPU %','RAM MB']},dataZoom:[{type:'inside'},{type:'slider'}],xAxis:{type:'category',data:x},yAxis:[{type:'value',name:'CPU %'},{type:'value',name:'RAM MB'}],series:[{name:'CPU %',type:'line',smooth:true,data:cpu},{name:'RAM MB',type:'line',smooth:true,yAxisIndex:1,data:ram}]});}
async function initLiveHistory(){liveSeries=await loadJson('/api/history/live');redrawLiveChart();}
modeButtons.forEach(b=>b.onclick=()=>{modeButtons.forEach(x=>x.classList.remove('active'));b.classList.add('active');chartMode=b.dataset.mode;sessionSelect.disabled=chartMode!=='session';redrawChart();});sessionSelect.onchange=redrawChart;window.addEventListener('resize',()=>{chart.resize();liveChart.resize();});(async function boot(){drawToggles();await refreshSnapshot();await initHistory();await initLiveHistory();setInterval(refreshSnapshot,1000);setInterval(initHistory,1000);setInterval(initLiveHistory,1000);})();
)JS";
    return js;
}

std::string WebSite::GetCurrentSnapshotJson() const {
    return BuildSnapshotJson(helper_.GetCurrentSnapshot());
}

std::string WebSite::Get24HoursHistoryJson() const {
    return BuildAggregatedSeriesJson(helper_.Get24HoursSeries());
}

std::string WebSite::GetSessionHistoryJson() const {
    return BuildSessionHistoryJson(helper_.GetSessionHistory());
}

std::string WebSite::GetLiveHistoryJson() const {
    return BuildLiveSeriesJson(helper_.GetLiveGraphBootstrap());
}

std::string WebSite::EscapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch == '"') out += "\\\"";
        else if (ch == '\\') out += "\\\\";
        else out += ch;
    }
    return out;
}

std::string WebSite::BuildSnapshotJson(const Telemetry::Snapshot& s) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"timestampMs\":" << s.timestampMs;
    oss << ",\"cpu\":{\"totalUsage\":" << s.cpu.totalUsage << ",\"perCoreUsage\":[";
    for (size_t i = 0; i < s.cpu.perCoreUsage.size(); ++i) { if (i) oss << ','; oss << s.cpu.perCoreUsage[i]; }
    oss << "]}";
    oss << ",\"memory\":{\"totalRAM\":" << s.memory.totalRAM << ",\"usedRAM\":" << s.memory.usedRAM << ",\"freeRAM\":" << s.memory.freeRAM << "}";
    oss << ",\"disk\":{\"disks\":[";
    for (size_t i = 0; i < s.disk.disks.size(); ++i) {
        const auto& d = s.disk.disks[i];
        if (i) oss << ',';
        oss << "{\"name\":\"" << EscapeJson(d.name) << "\",\"totalBytes\":" << d.totalBytes << ",\"freeBytes\":" << d.freeBytes << "}";
    }
    oss << "]}";
    oss << ",\"network\":{\"totalRxPerSec\":" << s.network.totalRxPerSec << ",\"totalTxPerSec\":" << s.network.totalTxPerSec << ",\"interfaces\":[";
    for (size_t i = 0; i < s.network.interfaces.size(); ++i) {
        const auto& n = s.network.interfaces[i];
        if (i) oss << ',';
        oss << "{\"name\":\"" << EscapeJson(n.name) << "\",\"ipv4\":\"" << EscapeJson(n.ipv4) << "\",\"rxBytesPerSec\":" << n.rxBytesPerSec << ",\"txBytesPerSec\":" << n.txBytesPerSec << "}";
    }
    oss << "]}";
    oss << ",\"system\":{\"osName\":\"" << EscapeJson(s.system.osName) << "\",\"kernelVersion\":\"" << EscapeJson(s.system.kernelVersion)
        << "\",\"hostname\":\"" << EscapeJson(s.system.hostname) << "\",\"uptimeSeconds\":" << s.system.uptimeSeconds << "}";
    oss << ",\"gpu\":{\"gpus\":[";
    for (size_t i = 0; i < s.gpu.gpus.size(); ++i) {
        const auto& g = s.gpu.gpus[i];
        if (i) oss << ',';
        oss << "{\"name\":\"" << EscapeJson(g.name) << "\",\"usagePercent\":" << g.usagePercent << ",\"vramUsedBytes\":" << g.vramUsedBytes << ",\"vramTotalBytes\":" << g.vramTotalBytes << "}";
    }
    oss << "]}";
    oss << "}";
    return oss.str();
}

std::string WebSite::BuildAggregatedSeriesJson(const std::vector<Telemetry::AggregatedSnapshot>& series) {
    std::ostringstream oss;
    oss << '[';
    for (size_t i = 0; i < series.size(); ++i) {
        const auto& e = series[i];
        if (i) oss << ',';
        oss << "{\"recordType\":" << static_cast<int>(e.recordType)
            << ",\"windowStartMs\":" << e.windowStartMs
            << ",\"windowEndMs\":" << e.windowEndMs
            << ",\"cpuAvg\":" << e.cpuAvg
            << ",\"ramUsedAvg\":" << e.ramUsedAvg << '}';
    }
    oss << ']';
    return oss.str();
}

std::string WebSite::BuildLiveSeriesJson(const std::vector<Telemetry::Snapshot>& series) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < series.size(); ++i) {
        if (i) oss << ",";
        oss << BuildSnapshotJson(series[i]);
    }
    oss << "]";
    return oss.str();
}

std::string WebSite::BuildSessionHistoryJson(const std::vector<std::vector<Telemetry::AggregatedSnapshot>>& sessions) {
    std::ostringstream oss;
    oss << '[';
    for (size_t i = 0; i < sessions.size(); ++i) {
        if (i) oss << ',';
        oss << BuildAggregatedSeriesJson(sessions[i]);
    }
    oss << ']';
    return oss.str();
}

} // namespace Web
