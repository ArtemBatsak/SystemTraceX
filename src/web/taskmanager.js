const formatBytes = (bytes) => {
  if (!bytes || bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return (bytes / Math.pow(k, i)).toFixed(2) + ' ' + sizes[i];
};

const formatTime = (ts) => {
  if (!ts) return 'unknown-time';
  return new Date(ts * 1000).toLocaleString();
};

const severityWeight = {
  Critical: 4,
  Error: 3,
  Warning: 2,
  Info: 1
};

function renderErrors(events) {
  const errorsBody = document.getElementById('errors-body');
  if (events.length === 0) {
    errorsBody.textContent = 'Нет серьёзных ошибок и крашей.';
    errorsBody.className = 'error-empty';
    return;
  }

  const grouped = events.reduce((acc, e) => {
    const key = e.source || 'Unknown';
    if (!acc[key]) acc[key] = [];
    acc[key].push(e);
    return acc;
  }, {});

  const sourceBlocks = Object.entries(grouped)
    .sort((a, b) => b[1].length - a[1].length)
    .map(([source, srcEvents]) => {
      srcEvents.sort((a, b) => (severityWeight[b.severity] || 0) - (severityWeight[a.severity] || 0));
      const rows = srcEvents.map((e) => {
        const badge = e.eventId ? `ID ${e.eventId}` : 'ID n/a';
        return `<div class="error-row sev-${e.severity}">
            <span class="sev-tag">${e.severity}</span>
            <span class="err-time">${formatTime(e.timestamp)}</span>
            <span class="err-id">${badge}</span>
            <div class="err-msg">${e.message}</div>
          </div>`;
      }).join('');
      return `<details class="error-group" open>
          <summary>${source} <span class="count">(${srcEvents.length})</span></summary>
          ${rows}
        </details>`;
    }).join('');

  errorsBody.className = '';
  errorsBody.innerHTML = sourceBlocks;
}

async function refreshTaskManager() {
  const [procRes, errRes] = await Promise.all([
    fetch('/api/processes'),
    fetch('/api/errors')
  ]);

  if (procRes.ok) {
    const data = await procRes.json();
    const body = document.getElementById('processes-body');
    body.innerHTML = '';
    (data.topProcesses || []).slice(0, 40).forEach((p) => {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td>${p.pid}</td><td>${p.name}</td><td>${Number(p.cpuUsage).toFixed(2)}</td><td>${formatBytes(p.memoryUsage)}</td><td>${Number(p.importanceScore).toFixed(2)}</td>`;
      body.appendChild(tr);
    });
  }

  if (errRes.ok) {
    const data = await errRes.json();
    const events = data.lastEvents || [];
    renderErrors(events);
  }
}

refreshTaskManager();
setInterval(refreshTaskManager, 1000);
