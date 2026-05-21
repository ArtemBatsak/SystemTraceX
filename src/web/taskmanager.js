const formatBytes = (bytes) => {
  if (!bytes || bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return (bytes / Math.pow(k, i)).toFixed(2) + ' ' + sizes[i];
};

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

  const errorsBody = document.getElementById('errors-body');
  if (errRes.ok) {
    const data = await errRes.json();
    const events = data.lastEvents || [];
    if (events.length === 0) {
      errorsBody.textContent = 'Нет системных ошибок.';
      errorsBody.className = 'error-empty';
    } else {
      errorsBody.className = '';
      errorsBody.innerHTML = events.map((e) => {
        return `<div class="sev-${e.severity}">[${e.severity}] ${e.source}: ${e.message} (eventId=${e.eventId})</div>`;
      }).join('');
    }
  }
}

refreshTaskManager();
setInterval(refreshTaskManager, 1000);
