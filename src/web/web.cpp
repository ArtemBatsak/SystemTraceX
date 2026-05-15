#include "web.h"

#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>

// =========================
// Constructor
// =========================
Web::Web(WebTelemetryHelper& helper)
    : helper_(helper)
{
}

// =========================
// START SERVER
// =========================
void Web::Start(int port)
{
    SetupRoutes();

    // запускаем telemetry thread
    std::thread([this]() {
        while (true)
        {
            Cache newCache;

            newCache.current = helper_.GetCurrentSnapshot();
            newCache.live = helper_.GetLiveGraphBootstrap();
            newCache.h24 = helper_.Get24HoursSeries();
            newCache.longSeries = helper_.GetLongRangeSeries();
            newCache.sessions = helper_.GetSessionHistory();

            {
                std::lock_guard<std::mutex> lock(cache_mtx_);
                cache_ = std::move(newCache);
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        }).detach();

    std::cout << "Server started on port " << port << std::endl;

    svr_.listen("0.0.0.0", port);
}

// =========================
// ROUTES
// =========================
void Web::SetupRoutes()
{
    svr_.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetIndexHtml(), "text/html; charset=utf-8");
        });

    svr_.Get("/style.css", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetStyleCss(), "text/css; charset=utf-8");
        });

    svr_.Get("/app.js", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetAppJs(), "application/javascript; charset=utf-8");
        });

    svr_.Get("/api/current", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetCurrentSnapshotJson(), "application/json");
        });

    svr_.Get("/api/history/live", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetLiveHistoryJson(), "application/json");
        });

    svr_.Get("/api/history/24h", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(Get24HoursHistoryJson(), "application/json");
        });

    svr_.Get("/api/history/long", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetLongHistoryJson(), "application/json");
        });

    svr_.Get("/api/history/sessions", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetSessionHistoryJson(), "application/json");
        });
}

// =========================
// HTML / CSS / JS
// =========================
std::string Web::GetIndexHtml() const
{
    return R"HTML(
<html>
<head>
    <link rel='stylesheet' href='/style.css'>
</head>
<body>
    <button id='burger'>☰</button>

    <aside id='menu' class='menu'>
        <h3>Снапшоты</h3>
        <ul>
            <li>CPU</li>
            <li>GPU</li>
            <li>NET</li>
            <li>RAM</li>
            <li>DISK</li>
        </ul>

        <label><input id='liveToggle' type='checkbox' checked> Live graph</label>
        <label><input id='h24Toggle' type='checkbox' checked> 24h graph</label>
        <label><input id='longToggle' type='checkbox' checked> Long graph</label>
    </aside>

    <main>
        <div id='liveChart' class='chart'></div>
        <div id='h24Chart' class='chart'></div>
        <div id='longChart' class='chart'></div>
    </main>

    <script src='https://cdn.jsdelivr.net/npm/echarts@5/dist/echarts.min.js'></script>
    <script src='/app.js'></script>
</body>
</html>
)HTML";
}

std::string Web::GetStyleCss() const
{
    return R"CSS(
body{
    margin:0;
    background:#111827;
    color:#e5e7eb;
    font-family:Arial;
}

#burger{
    position:fixed;
    left:10px;
    top:10px;
    z-index:5;
}

.menu{
    position:fixed;
    left:-260px;
    top:0;
    width:240px;
    height:100vh;
    background:#1f2937;
    padding:20px;
    transition:.2s;
}

.menu.open{
    left:0;
}

.chart{
    height:280px;
    margin:18px 20px;
    background:#1f2937;
    border-radius:12px;
}
)CSS";
}

std::string Web::GetAppJs() const
{
    return R"JS(
const menu = document.getElementById('menu');
document.getElementById('burger').onclick = () =>
    menu.classList.toggle('open');

const liveChart = echarts.init(document.getElementById('liveChart'));
const h24Chart  = echarts.init(document.getElementById('h24Chart'));
const longChart = echarts.init(document.getElementById('longChart'));

const pct = v => Math.max(0, Math.min(100, v));
const time = v => new Date(v).toLocaleTimeString();

async function j(u){ return (await fetch(u)).json(); }

function draw(c, x, series){
    c.setOption({
        tooltip:{trigger:'axis'},
        xAxis:{type:'category', data:x},
        yAxis:{type:'value', min:0, max:100},
        series
    });
}

async function refreshLive(){
    const d = await j('/api/history/live');

    draw(liveChart,
        d.map(r => time(r.timestampMs)),
        [
            {name:'CPU', type:'line', data:d.map(r=>pct(r.cpu.totalUsage))},
            {name:'GPU', type:'line', data:d.map(r=>pct((r.gpu.gpus[0]||{usagePercent:0}).usagePercent))},
            {name:'RAM', type:'line', data:d.map(r=>pct((r.memory.usedRAM*100)/Math.max(1,r.memory.totalRAM)))}
        ]
    );
}

function agg(d){
    return {
        x: d.map(r=>time(r.windowEndMs)),
        cpu: d.map(r=>pct(r.cpuAvg)),
        gpu: d.map(r=>pct(r.gpuAvg)),
        ram: d.map(r=>pct(r.ramPercentAvg)),
        disk: d.map(r=>pct(r.diskPercentAvg))
    };
}

async function refreshHistory(){
    const h = agg(await j('/api/history/24h'));

    draw(h24Chart, h.x, [
        {name:'CPU', type:'line', data:h.cpu},
        {name:'GPU', type:'line', data:h.gpu},
        {name:'RAM', type:'line', data:h.ram},
        {name:'DISK', type:'line', data:h.disk}
    ]);

    const l = agg(await j('/api/history/long'));

    draw(longChart, l.x, [
        {name:'CPU', type:'line', data:l.cpu},
        {name:'GPU', type:'line', data:l.gpu},
        {name:'RAM', type:'line', data:l.ram},
        {name:'DISK', type:'line', data:l.disk}
    ]);
}

function bindToggle(id, el){
    document.getElementById(id).onchange = e =>
        el.style.display = e.target.checked ? 'block' : 'none';
}

bindToggle('liveToggle', document.getElementById('liveChart'));
bindToggle('h24Toggle', document.getElementById('h24Chart'));
bindToggle('longToggle', document.getElementById('longChart'));

setInterval(refreshLive, 1000);
setInterval(refreshHistory, 1000);

refreshLive();
refreshHistory();
)JS";
}

// =========================
// JSON API (CACHE SAFE)
// =========================
std::string Web::GetCurrentSnapshotJson() const
{
    std::lock_guard<std::mutex> lock(cache_mtx_);
    return BuildSnapshotJson(cache_.current);
}

std::string Web::GetLiveHistoryJson() const
{
    std::lock_guard<std::mutex> lock(cache_mtx_);
    return BuildLiveSeriesJson(cache_.live);
}

std::string Web::Get24HoursHistoryJson() const
{
    std::lock_guard<std::mutex> lock(cache_mtx_);
    return BuildAggregatedSeriesJson(cache_.h24);
}

std::string Web::GetLongHistoryJson() const
{
    std::lock_guard<std::mutex> lock(cache_mtx_);
    return BuildAggregatedSeriesJson(cache_.longSeries);
}

std::string Web::GetSessionHistoryJson() const
{
    std::lock_guard<std::mutex> lock(cache_mtx_);
    return BuildSessionHistoryJson(cache_.sessions);
}



std::string Web::BuildSnapshotJson(const Telemetry::Snapshot& s) const
{
    std::ostringstream oss;

    oss << "{"
        << "\"timestampMs\":" << s.timestampMs
        << ",\"cpu\":{\"totalUsage\":" << s.cpu.totalUsage << "}"
        << ",\"memory\":{\"totalRAM\":" << s.memory.totalRAM
        << ",\"usedRAM\":" << s.memory.usedRAM << "}"
        << ",\"gpu\":{\"gpus\":[";

    for (size_t i = 0; i < s.gpu.gpus.size(); ++i)
    {
        if (i) oss << ",";
        oss << "{\"usagePercent\":" << s.gpu.gpus[i].usagePercent << "}";
    }

    oss << "]}}";
    return oss.str();
}


std::string Web::BuildAggregatedSeriesJson(
    const std::vector<Telemetry::AggregatedSnapshot>& series) const
{
    std::ostringstream oss;
    oss << "[";

    for (size_t i = 0; i < series.size(); ++i)
    {
        const auto& e = series[i];
        if (i) oss << ",";

        oss << "{"
            << "\"recordType\":" << (int)e.recordType
            << ",\"windowEndMs\":" << e.windowEndMs
            << ",\"cpuAvg\":" << e.cpuAvg
            << ",\"gpuAvg\":" << e.gpuAvg
            << ",\"ramPercentAvg\":" << e.ramPercentAvg
            << ",\"diskPercentAvg\":" << e.diskPercentAvg
            << "}";
    }

    oss << "]";
    return oss.str();
}

std::string Web::BuildLiveSeriesJson(
    const std::vector<Telemetry::Snapshot>& series) const
{
    std::ostringstream oss;
    oss << "[";

    for (size_t i = 0; i < series.size(); ++i)
    {
        if (i) oss << ",";
        oss << BuildSnapshotJson(series[i]);
    }

    oss << "]";
    return oss.str();
}


std::string Web::BuildSessionHistoryJson(
    const std::vector<std::vector<Telemetry::AggregatedSnapshot>>& sessions) const
{
    std::ostringstream oss;
    oss << "[";

    for (size_t i = 0; i < sessions.size(); ++i)
    {
        if (i) oss << ",";
        oss << BuildAggregatedSeriesJson(sessions[i]);
    }

    oss << "]";

    return oss.str();
}