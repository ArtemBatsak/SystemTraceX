const cards = document.getElementById('cards');
const tableToggles = document.getElementById('tableToggles');
const tablesPanel = document.getElementById('tablesPanel');
const sessionSelect = document.getElementById('sessionSelect');
const modeButtons = [...document.querySelectorAll('.history-mode button')];
const chart = echarts.init(document.getElementById('historyChart'));

const tableState = new Map([
  ['cpu', true], ['memory', true], ['disk', true], ['network', true], ['system', true], ['gpu', true]
]);
let chartMode = '24h';
let sessions = [];
let series24h = [];

const fmtMb = v => (v / (1024 * 1024)).toFixed(1);
const fmtGb = v => (v / (1024 * 1024 * 1024)).toFixed(2);
const fmtTime = ms => new Date(ms).toLocaleTimeString();

async function loadJson(url) { const r = await fetch(url); return r.json(); }

function drawCards(s) {
  cards.innerHTML = [
    ['CPU', `${s.cpu.totalUsage.toFixed(1)}%`],
    ['RAM used', `${fmtMb(s.memory.usedRAM)} MB`],
    ['Network RX', `${s.network.totalRxPerSec.toFixed(0)} B/s`],
    ['Network TX', `${s.network.totalTxPerSec.toFixed(0)} B/s`],
    ['Disks', String(s.disk.disks.length)],
    ['GPU count', String(s.gpu.gpus.length)]
  ].map(([k,v]) => `<article class="card"><div class="k">${k}</div><div class="v">${v}</div></article>`).join('');
}

function table(title, id, html) {
  const hidden = !tableState.get(id);
  return `<section class="table-wrap" style="display:${hidden ? 'none' : 'block'}"><h3>${title}</h3>${html}</section>`;
}

function drawTables(s) {
  const cpuRows = s.cpu.perCoreUsage.map((v, i) => `<tr><td>Core ${i}</td><td>${v.toFixed(1)}%</td></tr>`).join('');
  const diskRows = s.disk.disks.map(d => `<tr><td>${d.name}</td><td>${fmtGb(d.freeBytes)} GB</td><td>${fmtGb(d.totalBytes)} GB</td></tr>`).join('');
  const netRows = s.network.interfaces.map(n => `<tr><td>${n.name}</td><td>${n.ipv4}</td><td>${n.rxBytesPerSec.toFixed(0)}</td><td>${n.txBytesPerSec.toFixed(0)}</td></tr>`).join('');
  const gpuRows = s.gpu.gpus.map(g => `<tr><td>${g.name}</td><td>${g.usagePercent.toFixed(1)}%</td><td>${fmtMb(g.vramUsedBytes)} MB</td><td>${fmtMb(g.vramTotalBytes)} MB</td></tr>`).join('');

  tablesPanel.innerHTML =
    table('CPU', 'cpu', `<table><tr><th>Core</th><th>Usage</th></tr>${cpuRows}</table>`) +
    table('Memory', 'memory', `<table><tr><th>Used</th><th>Free</th><th>Total</th></tr><tr><td>${fmtMb(s.memory.usedRAM)} MB</td><td>${fmtMb(s.memory.freeRAM)} MB</td><td>${fmtMb(s.memory.totalRAM)} MB</td></tr></table>`) +
    table('Disk', 'disk', `<table><tr><th>Name</th><th>Free</th><th>Total</th></tr>${diskRows}</table>`) +
    table('Network', 'network', `<table><tr><th>Name</th><th>IPv4</th><th>RX B/s</th><th>TX B/s</th></tr>${netRows}</table>`) +
    table('System', 'system', `<table><tr><th>OS</th><th>Kernel</th><th>Host</th><th>Uptime sec</th></tr><tr><td>${s.system.osName}</td><td>${s.system.kernelVersion}</td><td>${s.system.hostname}</td><td>${s.system.uptimeSeconds}</td></tr></table>`) +
    table('GPU', 'gpu', `<table><tr><th>Name</th><th>Usage</th><th>VRAM used</th><th>VRAM total</th></tr>${gpuRows}</table>`);
}

function drawToggles() {
  tableToggles.innerHTML = [...tableState.keys()].map(key => `<label><input type="checkbox" data-key="${key}" ${tableState.get(key)?'checked':''}/> ${key}</label>`).join('');
  tableToggles.querySelectorAll('input').forEach(i => i.onchange = () => { tableState.set(i.dataset.key, i.checked); refreshSnapshot(); });
}

function asMetricSeries(rows) {
  const filtered = rows.filter(r => r.recordType === 0);
  return {
    x: filtered.map(r => fmtTime(r.windowEndMs)),
    cpu: filtered.map(r => r.cpuAvg),
    ram: filtered.map(r => r.ramUsedAvg / (1024*1024))
  };
}

function redrawChart() {
  const source = chartMode === '24h' ? series24h : (sessions[sessionSelect.value] || []);
  const p = asMetricSeries(source);
  chart.setOption({
    tooltip: { trigger: 'axis' },
    legend: { data: ['CPU %', 'RAM MB'] },
    dataZoom: [{ type: 'inside' }, { type: 'slider' }],
    xAxis: { type: 'category', data: p.x },
    yAxis: [{ type: 'value', name: 'CPU %' }, { type: 'value', name: 'RAM MB' }],
    series: [
      { name: 'CPU %', type: 'line', smooth: true, data: p.cpu },
      { name: 'RAM MB', type: 'line', smooth: true, yAxisIndex: 1, data: p.ram }
    ]
  });
}

async function refreshSnapshot() {
  const s = await loadJson('/api/current');
  drawCards(s); drawTables(s);
}

async function initHistory() {
  series24h = await loadJson('/api/history/24h');
  sessions = await loadJson('/api/history/sessions');
  sessionSelect.innerHTML = sessions.map((_, i) => `<option value="${i}">Session ${i + 1}</option>`).join('');
  redrawChart();
}

modeButtons.forEach(b => b.onclick = () => {
  modeButtons.forEach(x => x.classList.remove('active'));
  b.classList.add('active');
  chartMode = b.dataset.mode;
  sessionSelect.disabled = chartMode !== 'session';
  redrawChart();
});
sessionSelect.onchange = redrawChart;
window.addEventListener('resize', () => chart.resize());

(async function boot() {
  drawToggles();
  await refreshSnapshot();
  await initHistory();
  setInterval(refreshSnapshot, 1000);
  setInterval(initHistory, 1000);
})();
