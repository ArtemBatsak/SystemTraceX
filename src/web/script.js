// ======================== GLOBAL CONTEXTS AND STATES ========================
let currentChartMode = 'disabled'; 
const HISTORY_POINTS = 600; 
let metricDataHistory = {}; 
window.chartCache = false; 
let isUiFrozen = false; 

let snapshotInterval = null;     
let chartUpdateInterval = null;   

let globalTotalRamBytes = 0;      
let eChartInstance = null;        

const DISK_COLORS = ['#E91E63', '#9C27B0', '#673AB7', '#3F51B5', '#009688'];
let diskColorIndex = 0;

const MAX_ALLOWED_GAP_MS = 15 * 60 * 1000; 

// ======================== DATA FORMATTING UTILITIES ========================
const formatBytes = bytes => {
    if (!bytes || bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
};

function getStatusColor(percent) {
    if (percent < 60) return '#00e676'; 
    if (percent < 85) return '#ffb300'; 
    return '#ff5252';          
}

// ======================== LAYOUT VIEW & MINIMAL UTILITIES ========================

window.toggleFreezeMode = function() {
    isUiFrozen = !isUiFrozen;
    const fBtn = document.getElementById('freeze-btn');
    const dBtn = document.getElementById('drawer-freeze-btn');
    
    [fBtn, dBtn].forEach(btn => {
        if (btn) {
            if (isUiFrozen) {
                btn.classList.add('paused');
                btn.textContent = '▶ Resume Dashboard';
            } else {
                btn.classList.remove('paused');
                btn.textContent = '⏸ Pause Updates';
            }
        }
    });
};

window.toggleMinimalMode = function() {
    const body = document.getElementById('body');
    const btn = document.getElementById('toggle-minimal-btn');
    body.classList.toggle('minimal-mode');
    
    const isMinimal = body.classList.contains('minimal-mode');
    if(btn) {
        if(isMinimal) btn.classList.add('active');
        else btn.classList.remove('active');
    }
    
    const drawerBtn = document.getElementById('drawer-minimal-btn');
    if (drawerBtn) {
        drawerBtn.textContent = isMinimal ? '📺 Standard View' : '✨ Compact View';
    }

    // Force an inline evaluation update of GPU header text schema modifications
    const primaryGpuTitle = document.getElementById('gpu-header-title');
    if (primaryGpuTitle && window.lastSnapshotCache && window.lastSnapshotCache.gpus && window.lastSnapshotCache.gpus.length > 0) {
        primaryGpuTitle.textContent = isMinimal ? "GPU" : `GPU: ${window.lastSnapshotCache.gpus[0].name}`;
    }

    document.getElementById('compact-drawer').classList.remove('show');
    
    if (eChartInstance) {
        setTimeout(() => eChartInstance.resize(), 50);
    }
};

window.toggleStandardBurger = function(event) {
    event.stopPropagation();
    const menu = document.getElementById('standard-burger-menu');
    menu.style.display = menu.style.display === 'flex' ? 'none' : 'flex';
};

window.toggleCompactDrawer = function(event) {
    event.stopPropagation();
    document.getElementById('compact-drawer').classList.toggle('show');
};

window.syncSelects = function(val) {
    const selects = document.querySelectorAll('.chart-select');
    selects.forEach(sel => sel.value = val);
};

window.toggleCardVisibility = function(checkbox) {
    const targetId = checkbox.getAttribute('data-target');
    const targets = document.querySelectorAll(`[data-target="${targetId}"]`);
    
    targets.forEach(chk => chk.checked = checkbox.checked);

    const targetElement = document.getElementById(targetId);
    if (targetElement) {
        if(checkbox.checked) {
            targetElement.classList.add('visible-card');
            targetElement.style.setProperty('display', '', '');
        } else {
            targetElement.classList.remove('visible-card');
            targetElement.style.setProperty('display', 'none', 'important');
        }
    }
};

document.addEventListener('click', () => {
    document.getElementById('compact-drawer').classList.remove('show');
    const stdMenu = document.getElementById('standard-burger-menu');
    if (stdMenu) stdMenu.style.display = 'none';
});

document.querySelectorAll('.compact-drawer').forEach(el => {
    el.addEventListener('click', (e) => e.stopPropagation());
});

window.addEventListener('resize', () => {
    if (eChartInstance) eChartInstance.resize();
});


// ======================== ECHARTS ARCHITECTURE IMPLEMENTATION ========================

function initializeChartObject(chartOptions) {
    const container = document.getElementById('master_chart');
    if (!eChartInstance) {
        eChartInstance = echarts.init(container, 'dark');
    }
    eChartInstance.clear();
    eChartInstance.setOption(chartOptions);
    window.chartCache = true;
}

function createBaseChartConfig(titleText, seriesArray) {
    return {
        title: { text: titleText, left: 'center', textStyle: { color: '#e2e8f0', fontSize: 14, fontWeight: 'normal' } },
        backgroundColor: 'transparent',
        tooltip: { trigger: 'axis', backgroundColor: '#161925', borderColor: 'rgba(255,255,255,0.1)', textStyle: { color: '#fff' } },
        legend: { bottom: '0', textStyle: { color: '#94a3b8' }, type: 'scroll' },
        grid: { top: '70', left: '30', right: '30', bottom: '60', containLabel: true },
        // Полностью убираем панель с кнопками (скачивание, зум, сброс)
        toolbox: {
            show: false
        },
        dataZoom: [
            { type: 'inside', realtime: true, start: 0, end: 100 },
            { type: 'slider', realtime: true, start: 0, end: 100, bottom: 25, height: 15, textStyle: { color: '#94a3b8' } }
        ],
        xAxis: { type: 'time', splitLine: { show: true, lineStyle: { color: 'rgba(255,255,255,0.04)' } }, axisLabel: { color: '#94a3b8' } },
        yAxis: { type: 'value', min: 0, max: 100, splitLine: { show: true, lineStyle: { color: 'rgba(255,255,255,0.04)' } }, axisLabel: { color: '#94a3b8' } },
        series: seriesArray
    };
}
// ======================== DATA PIPELINES HISTORY ENGINE ========================

window.handleChartModeChange = async function(mode) {
    currentChartMode = mode;
    const chartsContainer = document.getElementById('charts-container');
    const chartsTitle = document.getElementById('charts-title');

    if (chartUpdateInterval) {
        clearInterval(chartUpdateInterval);
        chartUpdateInterval = null;
    }

    if (mode === 'disabled') {
        chartsContainer.style.display = 'none';
        window.chartCache = false;
        return;
    }

    chartsContainer.style.display = 'block';
    window.chartCache = false; 

    if (mode === 'live') {
        chartsTitle.textContent = 'System Load Operations Framework (Live Timeline)';
        await loadLiveHistoryAndSetupChart();
    } 
    else if (mode === '24h') {
        chartsTitle.textContent = '24-Hour Consolidated System Operational Profile';
        await loadAggregatedHistoryAndSetupChart('/api/telemetry/24h');
        chartUpdateInterval = setInterval(() => {
            loadAggregatedHistoryAndSetupChart('/api/telemetry/24h');
        }, 10000); 
    } 
    else if (mode === 'long') {
        chartsTitle.textContent = 'Extended Historical Analytical Analysis (Long Range)';
        await loadAggregatedHistoryAndSetupChart('/api/telemetry/long');
    }
};

async function loadLiveHistoryAndSetupChart() {
    try {
        const response = await fetch('/api/telemetry/live');
        if (!response.ok) throw new Error('Live stream connection fault');
        const historyArray = await response.json(); 

        metricDataHistory = {}; 
        let cpuData = [], ramData = [], gpuData = [];
        let diskTracesData = {};

        if (Array.isArray(historyArray) && historyArray.length > 0) {
            historyArray.forEach(point => {
                const timestamp = point.t ? new Date(point.t).getTime() : new Date().getTime();
                cpuData.push([timestamp, point.cpu || 0]);
                gpuData.push([timestamp, point.gpu || 0]);

                let rPct = 0;
                if (point.ram) {
                    rPct = point.ram.percent !== undefined ? point.ram.percent : ((point.ram.used / point.ram.total) * 100);
                }
                ramData.push([timestamp, rPct]);

                if (point.disks && point.disks.length > 0) {
                    point.disks.forEach(d => {
                        if (!diskTracesData[d.name]) diskTracesData[d.name] = [];
                        diskTracesData[d.name].push([timestamp, d.percent || 0]);
                    });
                }
            });
        }

        if (cpuData.length === 0) {
            const now = new Date().getTime();
            for(let i=0; i<HISTORY_POINTS; i++) {
                const pastTime = now - (HISTORY_POINTS - i) * 1000;
                cpuData.push([pastTime, 0]); ramData.push([pastTime, 0]); gpuData.push([pastTime, 0]);
            }
        }

        metricDataHistory['cpu'] = cpuData;
        metricDataHistory['ram'] = ramData;
        metricDataHistory['gpu'] = gpuData;

        const series = [
            { name: 'CPU', type: 'line', showSymbol: false, data: cpuData, itemStyle: { color: '#ff9f43' }, lineStyle: { width: 2 } }, 
            { name: 'RAM', type: 'line', showSymbol: false, data: ramData, itemStyle: { color: '#10ac84' }, lineStyle: { width: 2 } }, 
            { name: 'GPU', type: 'line', showSymbol: false, data: gpuData, itemStyle: { color: '#0abde3' }, lineStyle: { width: 2 } }
        ];

        Object.keys(diskTracesData).forEach(diskName => {
            metricDataHistory[diskName] = diskTracesData[diskName];
            const color = DISK_COLORS[diskColorIndex % DISK_COLORS.length];
            diskColorIndex++;
            series.push({ name: `${diskName} Utilization`, type: 'line', showSymbol: false, data: diskTracesData[diskName], itemStyle: { color: color }, lineStyle: { width: 1.5 } });
        });

        const options = createBaseChartConfig('Resource Usage Timeline (%)', series);
        initializeChartObject(options);

    } catch (error) {
        console.error('Failure evaluating live charting pipeline:', error);
        setupEmptyMasterChart();
    }
}

async function loadAggregatedHistoryAndSetupChart(apiUrl) {
    try {
        const response = await fetch(apiUrl);
        if (!response.ok) throw new Error(`Aggregated payload failed: ${apiUrl}`);
        const dataArray = await response.json();

        if (!Array.isArray(dataArray) || dataArray.length === 0) {
            setupEmptyMasterChart();
            return;
        }

        const metricsPoints = dataArray.filter(p => p.type === 'Metric');
        if (metricsPoints.length === 0) {
            setupEmptyMasterChart();
            return;
        }

        metricsPoints.sort((a, b) => ((a.windowStartMs + a.windowEndMs) / 2) - ((b.windowStartMs + b.windowEndMs) / 2));

        const cpuAvg = [], cpuMin = [], cpuMax = [];
        const gpuAvg = [], gpuMin = [], gpuMax = [];
        const ramAvg = [], ramMin = [], ramMax = [];

        const divisor = globalTotalRamBytes > 0 ? globalTotalRamBytes : 16 * 1024 * 1024 * 1024; 
        let lastTimestamp = null;

        metricsPoints.forEach(point => {
            const midTime = Math.floor((point.windowStartMs + point.windowEndMs) / 2);

            if (lastTimestamp !== null && (midTime - lastTimestamp) > MAX_ALLOWED_GAP_MS) {
                const stepGapTimestamp = lastTimestamp + 1000; 
                cpuAvg.push([stepGapTimestamp, null]); cpuMin.push([stepGapTimestamp, null]); cpuMax.push([stepGapTimestamp, null]);
                gpuAvg.push([stepGapTimestamp, null]); gpuMin.push([stepGapTimestamp, null]); gpuMax.push([stepGapTimestamp, null]);
                ramAvg.push([stepGapTimestamp, null]); ramMax.push([stepGapTimestamp, null]); ramMax.push([stepGapTimestamp, null]);
            }
            lastTimestamp = midTime;

            cpuAvg.push([midTime, point.cpu ? point.cpu.avg : 0]);
            cpuMax.push([midTime, point.cpu ? point.cpu.max : 0]);
            cpuMin.push([midTime, point.cpu ? point.cpu.min : 0]);

            gpuAvg.push([midTime, point.gpu ? point.gpu.avg : 0]);
            gpuMax.push([midTime, point.gpu ? point.gpu.max : 0]);
            gpuMin.push([midTime, point.gpu ? point.gpu.min : 0]);

            if (point.ram) {
                ramAvg.push([midTime, (point.ram.usedAvg / divisor) * 100]);
                ramMax.push([midTime, (point.ram.usedMax / divisor) * 100]);
                ramMin.push([midTime, (point.ram.usedMin / divisor) * 100]);
            } else {
                ramAvg.push([midTime, 0]); ramMax.push([midTime, 0]); ramMin.push([midTime, 0]);
            }
        });

        const series = [
            { name: 'CPU Avg', type: 'line', showSymbol: false, data: cpuAvg, itemStyle: { color: '#ff9f43' }, lineStyle: { width: 2.5 } },
            { name: 'CPU Max', type: 'line', showSymbol: false, data: cpuMax, itemStyle: { color: '#ffb142' }, lineStyle: { width: 1, type: 'dashed' } },
            { name: 'CPU Min', type: 'line', showSymbol: false, data: cpuMin, itemStyle: { color: '#cc8e35' }, lineStyle: { width: 1, type: 'dashed' } },
            { name: 'RAM Avg', type: 'line', showSymbol: false, data: ramAvg, itemStyle: { color: '#10ac84' }, lineStyle: { width: 2.5 } },
            { name: 'RAM Max', type: 'line', showSymbol: false, data: ramMax, itemStyle: { color: '#1dd1a1' }, lineStyle: { width: 1, type: 'dashed' } },
            { name: 'RAM Min', type: 'line', showSymbol: false, data: ramMin, itemStyle: { color: '#108564' }, lineStyle: { width: 1, type: 'dashed' } },
            { name: 'GPU Avg', type: 'line', showSymbol: false, data: gpuAvg, itemStyle: { color: '#0abde3' }, lineStyle: { width: 2.5 } },
            { name: 'GPU Max', type: 'line', showSymbol: false, data: gpuMax, itemStyle: { color: '#48dbfb' }, lineStyle: { width: 1, type: 'dashed' } },
            { name: 'GPU Min', type: 'line', showSymbol: false, data: gpuMin, itemStyle: { color: '#0197b5' }, lineStyle: { width: 1, type: 'dashed' } }
        ];

        const options = createBaseChartConfig('Historical Consolidated Operational Analysis (%)', series);
        initializeChartObject(options);

    } catch (error) {
        console.error('Aggregated pipeline error:', error);
        setupEmptyMasterChart();
    }
}

function setupEmptyMasterChart() {
    metricDataHistory = {};
    const now = new Date().getTime();
    let cpuData = [], ramData = [], gpuData = [];

    for(let i=0; i<HISTORY_POINTS; i++) {
        const pastTime = now - (HISTORY_POINTS - i) * 1000;
        cpuData.push([pastTime, 0]); ramData.push([pastTime, 0]); gpuData.push([pastTime, 0]);
    }
    
    metricDataHistory['cpu'] = cpuData;
    metricDataHistory['ram'] = ramData;
    metricDataHistory['gpu'] = gpuData;

    const options = createBaseChartConfig('No Telemetry Data Available Stream Interface', [
        { name: 'CPU', type: 'line', showSymbol: false, data: cpuData, itemStyle: { color: '#ff9f43' } },
        { name: 'RAM', type: 'line', showSymbol: false, data: ramData, itemStyle: { color: '#10ac84' } },
        { name: 'GPU', type: 'line', showSymbol: false, data: gpuData, itemStyle: { color: '#0abde3' } }
    ]);
    initializeChartObject(options);
}

function addNewDiskTrace(diskName, currentTimestamp) {
    if (metricDataHistory.hasOwnProperty(diskName)) return; 
    const color = DISK_COLORS[diskColorIndex % DISK_COLORS.length];
    diskColorIndex++;

    const paddingData = [];
    for (let i = 0; i < HISTORY_POINTS; i++) {
        paddingData.push([currentTimestamp - (HISTORY_POINTS - i) * 1000, 0]);
    }
    metricDataHistory[diskName] = paddingData;

    if (eChartInstance && currentChartMode === 'live') {
        const currentOptions = eChartInstance.getOption();
        currentOptions.series.push({
            name: `${diskName} Utilization`, type: 'line', showSymbol: false, data: paddingData, itemStyle: { color: color }, lineStyle: { width: 1.5 }
        });
        eChartInstance.setOption(currentOptions);
    }
}

function pushDataToBuffer(traceName, newValue) {
    const dataBuffer = metricDataHistory[traceName];
    if (!dataBuffer) return;

    const timestamp = new Date().getTime();
    if (dataBuffer.length >= HISTORY_POINTS) dataBuffer.shift(); 
    dataBuffer.push([timestamp, newValue]);         
}

function redrawMasterChart() {
    if (!window.chartCache || !eChartInstance || currentChartMode !== 'live' || isUiFrozen) return;
    const seriesUpdates = Object.keys(metricDataHistory).map(key => {
        return {
            name: key === 'cpu' || key === 'ram' || key === 'gpu' ? key.toUpperCase() : `${key} Utilization`,
            data: metricDataHistory[key]
        };
    });
    eChartInstance.setOption({ series: seriesUpdates });
}


// ======================== DOM REALTIME UPDATE RENDERERS ========================

function updateCompactRingIndicators(metricPrefix, percentValue) {
    const ringFill = document.getElementById(`${metricPrefix}-ring-fill`);
    const txtVal = document.getElementById(`${metricPrefix}-compact-val`);
    
    if (ringFill && txtVal) {
        const radiusCircumference = 2 * Math.PI * 35; 
        const strokeOffsetMapping = radiusCircumference - (percentValue / 100) * radiusCircumference;
        
        ringFill.style.strokeDashoffset = strokeOffsetMapping;
        ringFill.style.stroke = getStatusColor(percentValue);
        txtVal.textContent = `${percentValue.toFixed(0)}%`;
        txtVal.style.color = getStatusColor(percentValue);
    }
}

function updateMetricsDisplay(data) {
    if (!data) return;

    // Cache object data into global frame window to enable state transition context retention
    window.lastSnapshotCache = data;

    if (data.ram && data.ram.total) globalTotalRamBytes = data.ram.total;

    // Direct background array loading pipelines (always operational)
    if (data.cpu && currentChartMode === 'live' && window.chartCache) pushDataToBuffer('cpu', data.cpu.usage);
    if (data.ram && currentChartMode === 'live' && window.chartCache) pushDataToBuffer('ram', (data.ram.used / data.ram.total) * 100);
    if (data.gpus && data.gpus.length > 0 && currentChartMode === 'live' && window.chartCache) pushDataToBuffer('gpu', data.gpus[0].usage);

    if (data.disks && data.disks.length > 0) {
        data.disks.forEach(disk => {
            const diskPercent = (disk.used / disk.total) * 100;
            if (currentChartMode === 'live' && window.chartCache) {
                if (!metricDataHistory.hasOwnProperty(disk.name)) addNewDiskTrace(disk.name, new Date().getTime());
                pushDataToBuffer(disk.name, diskPercent);
            }
        });
    }

    if (currentChartMode === 'live' && window.chartCache) redrawMasterChart();

    // Interrupt DOM execution blocks if view freeze constraints are activated
    if (isUiFrozen) return;

    const isMinimalView = document.getElementById('body').classList.contains('minimal-mode');

    // 1. CPU
    if (data.cpu) {
        const cpuVal = data.cpu.usage;
        document.getElementById('cpu-name').textContent = data.cpu.name.trim();
        document.getElementById('cpu-usage').textContent = cpuVal.toFixed(1) + '%';
        
        const fillBar = document.getElementById('cpu-fill');
        fillBar.style.width = cpuVal + '%';
        fillBar.style.backgroundColor = getStatusColor(cpuVal);

        updateCompactRingIndicators('cpu', cpuVal);
    }

    // 2. RAM
    if (data.ram) {
        const ramUsedPct = (data.ram.used / data.ram.total) * 100;
        document.getElementById('ram-total').textContent = formatBytes(data.ram.total);
        document.getElementById('ram-used').textContent = formatBytes(data.ram.used) + ` (${ramUsedPct.toFixed(1)}%)`;
        document.getElementById('ram-free').textContent = formatBytes(data.ram.free);
        
        const fillBar = document.getElementById('ram-fill');
        fillBar.style.width = ramUsedPct + '%';
        fillBar.style.backgroundColor = getStatusColor(ramUsedPct);

        updateCompactRingIndicators('ram', ramUsedPct);
    }

    // 3. GPU (Hides name completely during minimal modes tasks)
    if (data.gpus && data.gpus.length > 0) {
        const primaryGpu = data.gpus[0];
        const gpuVal = primaryGpu.usage;
        document.getElementById('gpu-header-title').textContent = isMinimalView ? "GPU" : `GPU: ${primaryGpu.name}`;
        document.getElementById('gpu-usage').textContent = gpuVal.toFixed(1) + '%';

        updateCompactRingIndicators('gpu', gpuVal);
    }

    // 4. DISKS RENDERING WORKER MAPPINGS (FIXED SEPARATE BLOCKS PATHS)
    if (data.disks && data.disks.length > 0) {
        const stdContainer = document.getElementById('snapshot-disks-container');
        const compactWrapper = document.getElementById('disks-compact-wrapper');
        
        if (stdContainer && compactWrapper) {
            stdContainer.innerHTML = '';
            compactWrapper.innerHTML = '';

            data.disks.forEach(disk => {
                const diskPercent = (disk.used / disk.total) * 100;

                // Standard interface injection block definitions
                const diskDiv = document.createElement('div');
                diskDiv.className = 'disk-item';
                diskDiv.innerHTML = `
                    <p><strong>Disk (${disk.name}):</strong> <span>${formatBytes(disk.used)} / ${formatBytes(disk.total)} (${diskPercent.toFixed(1)}%)</span></p>
                    <div class="usage-bar"><div class="usage-fill" style="width: ${diskPercent}%; background-color: ${getStatusColor(diskPercent)}"></div></div>
                `;
                stdContainer.appendChild(diskDiv);

                // ==================== COMPACT MODE DISKS TREATMENT (NAME + PERCENT ONLY) ====================
                const compactRow = document.createElement('div');
                compactRow.className = 'compact-disk-row';
                compactRow.innerHTML = `
                    <div class="compact-disk-name" title="${disk.name}">${disk.name}</div>
                    <div style="color: ${getStatusColor(diskPercent)}; font-weight: bold;">${diskPercent.toFixed(0)}%</div>
                `;
                compactWrapper.appendChild(compactRow);
            });
        }
    }

    // 5. System General Contexts
    if (data.system) {
        document.getElementById('system-hostname').textContent = data.system.hostname;
        document.getElementById('system-os').textContent = data.system.os;
        document.getElementById('system-arch').textContent = data.system.arch;
        document.getElementById('system-uptime').textContent = data.system.uptime + ' s';
        document.getElementById('system-vm').textContent = data.system.virtualization.runningInVM ? `Yes (${data.system.virtualization.vendor})` : 'No';
    }

    // 6. Network Interconnect Infrastructure
    if (data.network) {
        const rxString = formatBytes(data.network.rx);
        const txString = formatBytes(data.network.tx);
        
        document.getElementById('net-rx').textContent = rxString + '/s';
        document.getElementById('net-tx').textContent = txString + '/s';
        
        document.getElementById('net-compact-rx').textContent = rxString;
        document.getElementById('net-compact-tx').textContent = txString;

        const interfaceList = document.getElementById('interface-list');
        if (interfaceList && data.network.interfaces) {
            interfaceList.innerHTML = '';
            data.network.interfaces.forEach(iface => {
                if (iface.isUp) {
                    const li = document.createElement('li');
                    li.style.fontSize = '0.85em';
                    li.style.color = iface.isLoopback ? '#747d8c' : '#e2e8f0';
                    li.style.margin = '6px 0';
                    li.innerHTML = `🌐 <strong style="color:var(--accent); font-weight:600;">${iface.name}:</strong> ${iface.ipv4 || 'No IP'} (<span style="color:#00e676">⬇</span> ${formatBytes(iface.rxTotal)} | <span style="color:#0abde3">⬆</span> ${formatBytes(iface.txTotal)})`;
                    interfaceList.appendChild(li);
                }
            });
        }
    }
}

// ======================== API PULL CONTEXT TIMER REGISTRATION ========================

async function fetchAndRenderSnapshot() {
    try {
        const response = await fetch('/api/snapshot');
        if (!response.ok) throw new Error(`Snapshot interface fail: ${response.status}`);
        const data = await response.json();
        updateMetricsDisplay(data); 
    } catch (error) {
        console.error('System snapshot execution failure stack trace:', error);
    }
}

function startDashboard() {
    fetchAndRenderSnapshot(); 
    snapshotInterval = setInterval(fetchAndRenderSnapshot, 1000); 
}

document.addEventListener('DOMContentLoaded', startDashboard);