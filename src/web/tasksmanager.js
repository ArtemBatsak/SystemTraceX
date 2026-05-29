// ======================== STATE ========================
let trackedProcesses = [];
let selectedPath     = null;
let eChartInstance   = null;
let chartHistory     = {};       // { path: [[ts, cpu, ram, inst], ...] }
let liveInterval     = null;
let allSystemProcesses = [];     // нормализованный кэш
let modalOpen        = false;

// ======================== UTILS ========================
function getStatusColor(pct) {
    if (pct < 60)  return '#00e676';
    if (pct < 85)  return '#ffb300';
    return '#ff5252';
}

function fmtCpu(v)  { return (v == null || v === undefined) ? '—' : (+v).toFixed(1) + '%'; }
function fmtRam(v)  { return (v == null || v === undefined) ? '—' : formatBytes(v); }
function fmtInst(v) { return (v == null || v === undefined) ? '—' : String(v); }

function formatBytes(bytes) {
    if (!bytes || bytes === 0) return '0 B';
    const k = 1024, sizes = ['B','KB','MB','GB','TB'];
    const i = Math.floor(Math.log(Math.abs(bytes)) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

function escHtml(s) {
    return String(s)
        .replace(/&/g,'&amp;').replace(/</g,'&lt;')
        .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}
function escAttr(s) { return String(s).replace(/\\/g,'\\\\').replace(/'/g,"\\'"); }

// ======================== FETCH: TRACKED LIST ========================
async function fetchTracked() {
    try {
        const res = await fetch('/api/task-logger/tracked');
        if (!res.ok) throw new Error(res.status);
        const json = await res.json();
        trackedProcesses = json.tracked || [];
        renderTrackedList();
    } catch (e) {
        console.error('fetchTracked error:', e);
    }
}

// ======================== RENDER: TRACKED LIST ========================
function renderTrackedList() {
    const el      = document.getElementById('process-list');
    const countEl = document.getElementById('process-count');

    countEl.textContent = trackedProcesses.length
        ? `${trackedProcesses.length} process${trackedProcesses.length !== 1 ? 'es' : ''}`
        : '';

    if (!trackedProcesses.length) {
        el.innerHTML = `
            <div class="empty-state">
                <div class="empty-icon">📭</div>
                <p>No processes tracked yet. Click <strong>＋ Add Process</strong> to start.</p>
            </div>`;
        return;
    }

    el.innerHTML = '';
    trackedProcesses.forEach(proc => {
        const latest   = proc.latest || {};
        const cpuVal   = latest.cpu       ?? null;
        const ramVal   = latest.ram       ?? null;
        const instVal  = latest.instances ?? null;
        const isActive = proc.enabled;
        const isSel    = proc.path === selectedPath;
        const cpuColor = cpuVal != null ? getStatusColor(cpuVal) : 'var(--text-muted)';

        const row = document.createElement('div');
        row.className = `process-row${isActive ? '' : ' disabled'}${isSel ? ' selected' : ''}`;
        row.dataset.path = proc.path;

        row.innerHTML = `
            <div class="process-status-dot ${isActive ? 'active' : 'inactive'}"></div>
            <div class="process-info">
                <div class="process-name">${escHtml(proc.name)}</div>
                <div class="process-path">${escHtml(proc.shortPath || proc.path)}</div>
            </div>
            <div class="process-metrics">
                <div class="proc-metric">
                    <div class="proc-metric-val" style="color:${cpuColor}">${fmtCpu(cpuVal)}</div>
                    <div class="proc-metric-label">CPU</div>
                </div>
                <div class="proc-metric">
                    <div class="proc-metric-val">${fmtRam(ramVal)}</div>
                    <div class="proc-metric-label">RAM</div>
                </div>
                <div class="proc-metric">
                    <div class="proc-metric-val">${fmtInst(instVal)}</div>
                    <div class="proc-metric-label">Inst</div>
                </div>
            </div>
            <div class="process-actions" onclick="event.stopPropagation()">
                <label class="toggle-switch" title="${isActive ? 'Disable' : 'Enable'} tracking">
                    <input type="checkbox" ${isActive ? 'checked' : ''}
                        onchange="window.toggleEnabled('${escAttr(proc.path)}', this.checked)">
                    <span class="toggle-slider"></span>
                </label>
                <button class="btn btn-sm btn-danger"
                    onclick="window.removeProcess('${escAttr(proc.path)}')"
                    title="Remove">✕</button>
            </div>`;

        row.addEventListener('click', () => window.selectProcess(proc.path, proc.name));
        el.appendChild(row);
    });
}

// ======================== ACTIONS: TOGGLE / REMOVE ========================
window.toggleEnabled = async function(path, enabled) {
    try {
        await fetch('/api/task-logger/enabled', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ path, enabled })
        });
        const proc = trackedProcesses.find(p => p.path === path);
        if (proc) proc.enabled = enabled;
        renderTrackedList();
    } catch (e) { console.error('toggleEnabled error:', e); }
};

window.removeProcess = async function(path) {
    if (!confirm('Remove this process from tracking?')) return;
    try {
        await fetch('/api/task-logger/remove', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ path })
        });
        trackedProcesses = trackedProcesses.filter(p => p.path !== path);
        if (selectedPath === path) closeChart();
        delete chartHistory[path];
        renderTrackedList();
    } catch (e) { console.error('removeProcess error:', e); }
};

// ======================== CHART: SELECT PROCESS ========================
window.selectProcess = async function(path, name) {
    if (selectedPath === path) { closeChart(); return; }

    selectedPath = path;
    renderTrackedList();

    const section = document.getElementById('chart-section');
    section.style.display = 'block';
    document.getElementById('chart-proc-label').textContent = path;
    document.getElementById('chart-proc-title').textContent = name;

    // Инициализируем chart один раз
    if (!eChartInstance) {
        eChartInstance = echarts.init(document.getElementById('task-chart'), 'dark');
        window.addEventListener('resize', () => eChartInstance && eChartInstance.resize());
    }
    eChartInstance.clear();
    eChartInstance.showLoading({
        text: 'Loading history…',
        textColor: '#94a3b8',
        maskColor: 'rgba(15,17,26,0.6)'
    });

    if (liveInterval) { clearInterval(liveInterval); liveInterval = null; }

    // 1. Грузим историю
    chartHistory[path] = [];
    try {
        const res = await fetch('/api/task-logger/history');
        if (res.ok) {
            const json = await res.json();
            const found = (json.tracked || []).find(p => p.path === path);
            if (found && Array.isArray(found.history)) {
                chartHistory[path] = found.history.map(h => [
                    h.timestamp, h.cpu, h.ram, h.instances
                ]);
            }
        }
    } catch(e) { console.error('history fetch error:', e); }

    eChartInstance.hideLoading();

    // 2. Строим график из истории
    buildChart(path);

    // 3. Live: каждую секунду добавляем точку из /tracked
    liveInterval = setInterval(() => {
        if (!selectedPath) return;
        const proc = trackedProcesses.find(p => p.path === selectedPath);
        if (!proc || !proc.latest || !proc.latest.timestamp) return;

        const buf = chartHistory[selectedPath];
        if (!buf) return;

        const last = buf.length ? buf[buf.length - 1] : null;
        const pt   = proc.latest;
        if (!last || pt.timestamp > last[0]) {
            buf.push([pt.timestamp, pt.cpu, pt.ram, pt.instances]);
            if (buf.length > 1200) buf.shift();
            updateChartLive(selectedPath);
        }
    }, 1000);
};

window.closeChart = function() {
    selectedPath = null;
    if (liveInterval) { clearInterval(liveInterval); liveInterval = null; }
    document.getElementById('chart-section').style.display = 'none';
    renderTrackedList();
};

// ======================== CHART: BUILD ========================
function buildChart(path) {
    if (!eChartInstance) return;
    const buf = chartHistory[path] || [];

    const cpuData  = buf.map(p => [p[0], p[1] ?? null]);
    const ramData  = buf.map(p => [p[0], p[2] != null ? p[2] / (1024 * 1024) : null]);
    const instData = buf.map(p => [p[0], p[3] ?? null]);

    const option = {
        backgroundColor: 'transparent',
        tooltip: {
            trigger: 'axis',
            backgroundColor: '#161925',
            borderColor: 'rgba(255,255,255,0.1)',
            textStyle: { color: '#fff', fontSize: 12 },
            formatter: params => {
                if (!params.length) return '';
                const ts = new Date(params[0].value[0]);
                const timeStr = ts.toLocaleTimeString();
                let html = `<div style="margin-bottom:4px;color:#94a3b8;font-size:11px">${timeStr}</div>`;
                params.forEach(p => {
                    if (p.value[1] == null) return;
                    const v = p.seriesName === 'RAM (MB)'
                        ? p.value[1].toFixed(1) + ' MB'
                        : p.seriesName === 'Instances'
                        ? String(Math.round(p.value[1]))
                        : p.value[1].toFixed(2) + '%';
                    html += `<div>${p.marker}${p.seriesName}: <b>${v}</b></div>`;
                });
                return html;
            }
        },
        legend: { bottom: 0, textStyle: { color: '#94a3b8' }, type: 'scroll' },
        grid: { top: 40, left: 20, right: 60, bottom: 60, containLabel: true },
        toolbox: { show: false },
        dataZoom: [
            { type: 'inside', realtime: true },
            { type: 'slider', realtime: true, bottom: 25, height: 15, textStyle: { color: '#94a3b8' } }
        ],
        xAxis: {
            type: 'time',
            splitLine: { show: true, lineStyle: { color: 'rgba(255,255,255,0.04)' } },
            axisLabel: { color: '#94a3b8', fontSize: 11 }
        },
        yAxis: [
            {
                type: 'value', name: 'CPU %',
                min: 0, max: 100,
                splitLine: { show: true, lineStyle: { color: 'rgba(255,255,255,0.04)' } },
                axisLabel: { color: '#94a3b8', formatter: v => v + '%', fontSize: 11 }
            },
            {
                type: 'value', name: 'MB / Inst',
                min: 0,
                position: 'right',
                splitLine: { show: false },
                axisLabel: { color: '#94a3b8', fontSize: 11 }
            }
        ],
        series: [
            {
                id: 'cpu', name: 'CPU', type: 'line',
                showSymbol: false,
                data: cpuData,
                itemStyle: { color: '#ff9f43' },
                lineStyle: { width: 2 },
                areaStyle: { color: { type:'linear', x:0,y:0,x2:0,y2:1,
                    colorStops:[{offset:0,color:'rgba(255,159,67,0.25)'},{offset:1,color:'rgba(255,159,67,0)'}] }},
                animation: false, connectNulls: false, yAxisIndex: 0
            },
            {
                id: 'ram', name: 'RAM (MB)', type: 'line',
                showSymbol: false,
                data: ramData,
                itemStyle: { color: '#10ac84' },
                lineStyle: { width: 2 },
                areaStyle: { color: { type:'linear', x:0,y:0,x2:0,y2:1,
                    colorStops:[{offset:0,color:'rgba(16,172,132,0.2)'},{offset:1,color:'rgba(16,172,132,0)'}] }},
                animation: false, connectNulls: false, yAxisIndex: 1
            },
            {
                id: 'inst', name: 'Instances', type: 'line',
                showSymbol: false, step: 'end',
                data: instData,
                itemStyle: { color: '#5468ff' },
                lineStyle: { width: 1.5, type: 'dashed' },
                animation: false, connectNulls: false, yAxisIndex: 1
            }
        ]
    };

    eChartInstance.setOption(option, true);
}

// ======================== CHART: LIVE UPDATE ========================
function updateChartLive(path) {
    if (!eChartInstance) return;
    const buf = chartHistory[path] || [];

    eChartInstance.setOption({
        series: [
            { id: 'cpu',  data: buf.map(p => [p[0], p[1] ?? null]) },
            { id: 'ram',  data: buf.map(p => [p[0], p[2] != null ? p[2]/(1024*1024) : null]) },
            { id: 'inst', data: buf.map(p => [p[0], p[3] ?? null]) }
        ]
    }, { replaceMerge: [] });
}

// ======================== MODAL: ADD PROCESS ========================
window.openAddModal = async function() {
    modalOpen = true;
    document.getElementById('add-modal').classList.add('show');
    document.getElementById('proc-search').value = '';

    const listEl = document.getElementById('modal-proc-list');
    listEl.innerHTML = '<div class="modal-loading">Loading system processes…</div>';

    // Всегда грузим свежий список
    allSystemProcesses = [];

    try {
        const res = await fetch('/api/processes/all');
        if (!res.ok) throw new Error(res.status);
        const json = await res.json();

        // API возвращает: { totalProcesses, timestampMs, topProcesses: [...] }
        // или массив, или { processes: [...] }
        let raw = [];
        if (Array.isArray(json)) {
            raw = json;
        } else if (Array.isArray(json.topProcesses)) {
            raw = json.topProcesses;
        } else if (Array.isArray(json.processes)) {
            raw = json.processes;
        }

        // Нормализуем поля
        allSystemProcesses = raw.map(p => ({
            pid:    p.pid  ?? null,
            name:   p.name || '',
            // path может отсутствовать — тогда используем имя как идентификатор
            path:   p.path || p.exe || p.executablePath || p.filePath || p.name || '',
            cpu:    p.cpuUsage    ?? p.cpu    ?? 0,
            memory: p.memoryUsage ?? p.memory ?? p.ram ?? 0,
        })).filter(p => p.name); // убираем пустые

        renderModalList(allSystemProcesses);
    } catch(e) {
        console.error('openAddModal error:', e);
        listEl.innerHTML = '<div class="modal-loading" style="color:var(--color-danger)">Failed to load processes</div>';
    }

    setTimeout(() => document.getElementById('proc-search').focus(), 50);
};

window.closeAddModal = function(event) {
    if (event && event.target !== document.getElementById('add-modal')) return;
    closeModalDirect();
};

window.closeAddModalDirect = function() { closeModalDirect(); };

function closeModalDirect() {
    modalOpen = false;
    document.getElementById('add-modal').classList.remove('show');
}

document.addEventListener('keydown', e => {
    if (e.key === 'Escape' && modalOpen) closeModalDirect();
});

window.filterModalList = function(query) {
    const q = query.trim().toLowerCase();
    const filtered = q
        ? allSystemProcesses.filter(p =>
            (p.name || '').toLowerCase().includes(q) ||
            (p.path || '').toLowerCase().includes(q)
          )
        : allSystemProcesses;
    renderModalList(filtered);
};

function renderModalList(list) {
    const el = document.getElementById('modal-proc-list');

    if (!list.length) {
        el.innerHTML = '<div class="modal-loading">No processes found</div>';
        return;
    }

    const trackedPaths = new Set(trackedProcesses.map(p => p.path));

    el.innerHTML = '';
    list.forEach(proc => {
        const name    = proc.name || 'Unknown';
        const path    = proc.path || name;
        const already = trackedPaths.has(path);
        const cpuStr  = proc.cpu  != null ? (+proc.cpu).toFixed(1)  + '%'  : '';
        const memStr  = proc.memory > 0   ? formatBytes(proc.memory)        : '';

        const row = document.createElement('div');
        row.className = 'modal-proc-row';
        if (already) row.style.opacity = '0.45';

        row.innerHTML = `
            <div class="modal-proc-icon">${already ? '✅' : '⚙️'}</div>
            <div class="modal-proc-info">
                <div class="modal-proc-name">${escHtml(name)}</div>
                <div class="modal-proc-path">${escHtml(path !== name ? path : '')}</div>
            </div>
            <div style="display:flex;flex-direction:column;align-items:flex-end;gap:2px;flex-shrink:0">
                ${cpuStr ? `<span style="font-family:monospace;font-size:0.8rem;color:var(--color-warning)">${escHtml(cpuStr)}</span>` : ''}
                ${memStr ? `<span style="font-family:monospace;font-size:0.75rem;color:var(--text-muted)">${escHtml(memStr)}</span>` : ''}
                ${proc.pid != null ? `<span class="modal-proc-pid">PID ${proc.pid}</span>` : ''}
            </div>`;

        if (!already) {
            row.addEventListener('click', () => window.addProcessToTrack(name, path));
        } else {
            row.title = 'Already tracked';
            row.style.cursor = 'default';
        }

        el.appendChild(row);
    });
}

window.addProcessToTrack = async function(name, path) {
    // Сразу визуально помечаем строку в модале
    document.querySelectorAll('.modal-proc-row').forEach(row => {
        const pathEl = row.querySelector('.modal-proc-path');
        const nameEl = row.querySelector('.modal-proc-name');
        if ((pathEl && pathEl.textContent === path) ||
            (nameEl && nameEl.textContent === name && path === name)) {
            const iconEl = row.querySelector('.modal-proc-icon');
            if (iconEl) iconEl.textContent = '✅';
            row.style.opacity = '0.45';
            row.style.cursor = 'default';
            const clone = row.cloneNode(true); // клонируем без listeners
            row.replaceWith(clone);
        }
    });

    try {
        const res = await fetch('/api/task-logger/watch', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name, path, enabled: true })
        });
        if (!res.ok) {
            const txt = await res.text();
            throw new Error(`${res.status}: ${txt}`);
        }

        // Локально добавляем
        if (!trackedProcesses.find(p => p.path === path)) {
            trackedProcesses.push({ name, path, shortPath: path, enabled: true, latest: {} });
        }
        renderTrackedList();

    } catch(e) {
        console.error('addProcessToTrack error:', e);
        alert('Failed to add process:\n' + e.message);
    }
};

// ======================== POLL LOOP ========================
function startPolling() {
    fetchTracked();
    setInterval(fetchTracked, 1000);
}

document.addEventListener('DOMContentLoaded', startPolling);