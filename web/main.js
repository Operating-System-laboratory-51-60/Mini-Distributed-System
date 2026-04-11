const POLL_INTERVAL = 1000;
let lastResultsTotal = 0;

// ── Tab Switching ──
function switchTab(id) {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.tab-body').forEach(t => t.classList.remove('active'));
    document.querySelector(`[onclick="switchTab('${id}')"]`).classList.add('active');
    document.getElementById(`tab-${id}`).classList.add('active');
}

// ── File Input ──
document.getElementById('c-file-input').addEventListener('change', e => {
    const name = e.target.files[0]?.name;
    document.getElementById('file-label').textContent = name ? `Selected: ${name}` : 'Drop or click to select .c file';
});
document.getElementById('file-drop-zone').addEventListener('click', () => {
    document.getElementById('c-file-input').click();
});

// ── Terminal Logger ──
function log(msg, type = 'sys') {
    const t = document.getElementById('terminal-output');
    const d = document.createElement('div');
    d.className = `tl ${type}`;
    d.textContent = msg;
    t.appendChild(d);
    t.scrollTop = t.scrollHeight;
    while (t.children.length > 300) t.removeChild(t.firstChild);
}

// ── API Polling ──
async function fetchStatus() {
    try {
        const r = await fetch('/api/status');
        const d = await r.json();
        document.getElementById('local-ip').textContent = `${d.my_ip}:${d.my_port}`;
        document.getElementById('local-load-text').textContent = `${d.load_percent}%`;
        document.getElementById('local-load-bar').style.width = `${d.load_percent}%`;
        document.getElementById('local-queue-text').textContent = d.queue_depth;
        document.getElementById('local-active-text').textContent = `${d.active_tasks}/${d.max_tasks}`;
    } catch(e) {}
}

async function fetchPeers() {
    try {
        const r = await fetch('/api/peers');
        const d = await r.json();
        document.getElementById('peer-count').textContent = `${d.alive_count}/${d.peers.length}`;
        const container = document.getElementById('peers-container');
        
        if (d.peers.length === 0) {
            container.innerHTML = '<div class="peer-empty">No peers connected</div>';
            return;
        }
        
        let html = '';
        d.peers.forEach(p => {
            const cls = p.is_alive ? 'alive' : 'dead';
            html += `
                <div class="peer-card">
                    <div class="peer-dot ${cls}"></div>
                    <div class="peer-info">
                        <div class="peer-ip">${p.ip}:${p.port}</div>
                        <div class="peer-meta">Queue: ${p.queue_depth}</div>
                    </div>
                    <div class="peer-load">${p.load_percent}%</div>
                </div>`;
        });
        container.innerHTML = html;
    } catch(e) {}
}

async function fetchResults() {
    try {
        const r = await fetch('/api/results');
        const d = await r.json();
        const total = d.total || 0;
        document.getElementById('result-count').textContent = total;
        
        if (total > lastResultsTotal) {
            const newCount = total - lastResultsTotal;
            const startIdx = Math.max(0, d.results.length - newCount);
            for (let i = startIdx; i < d.results.length; i++) {
                const res = d.results[i];
                const cmd = res.command || 'task';
                log(`✓ #${res.task_id} [${cmd}] — ${res.execution_ms}ms`, 'ok');
                if (res.output && res.output.trim()) {
                    log(res.output.trim(), 'out');
                }
            }
            lastResultsTotal = total;
        }
    } catch(e) {}
}

setInterval(() => { fetchStatus(); fetchPeers(); fetchResults(); }, POLL_INTERVAL);
fetchStatus(); fetchPeers(); fetchResults();

// ── Discover ──
async function discoverPeers() {
    log('▸ Broadcasting UDP discovery...', 'cmd');
    try {
        const r = await fetch('/api/discover', { method: 'POST' });
        const d = await r.json();
        log(d.message || 'Discovery broadcast sent', 'ok');
    } catch(e) { log(`Discovery failed: ${e}`, 'err'); }
}

// ── Task Dispatch ──
async function submitSimpleTask() {
    const type = document.getElementById('task-type').value;
    const arg = document.getElementById('task-arg').value;
    if (!arg) { alert('Enter a command or value'); return; }
    
    log(`▸ ${type}: ${arg}`, 'cmd');
    try {
        const r = await fetch('/api/task', {
            method: 'POST',
            headers: {'Content-Type':'application/json'},
            body: JSON.stringify({type, arg})
        });
        const d = await r.json();
        if (d.status === 'success') {
            log(`Task #${d.task_id} → ${d.where}`, 'ok');
            document.getElementById('task-arg').value = '';
        } else {
            log(`Error: ${d.error}`, 'err');
        }
    } catch(e) { log(`Network error: ${e}`, 'err'); }
}

async function submitCodeTask() {
    const input = document.getElementById('c-file-input');
    if (!input.files.length) { alert('Select a file first'); return; }
    const file = input.files[0];
    if (file.size > 65536) { alert('File must be < 64KB'); return; }
    
    log(`▸ Uploading ${file.name}...`, 'cmd');
    const reader = new FileReader();
    reader.onload = async (e) => {
        try {
            const r = await fetch('/api/upload', {
                method: 'POST',
                headers: {'Content-Type':'application/json'},
                body: JSON.stringify({filename: file.name, code: e.target.result})
            });
            const d = await r.json();
            if (d.status === 'success') {
                log(`${file.name} → ${d.where} (#${d.task_id})`, 'ok');
                input.value = '';
                document.getElementById('file-label').textContent = 'Drop or click to select .c file';
            } else { log(`Error: ${d.error}`, 'err'); }
        } catch(err) { log(`Network error: ${err}`, 'err'); }
    };
    reader.readAsText(file);
}
