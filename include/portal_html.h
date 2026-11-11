#pragma once

const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="theme-color" content="#060913">
<title>AetherPulse Extreme - Smart Repeater</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&family=JetBrains+Mono:wght@400;600&display=swap" rel="stylesheet">
<style>
  :root {
    --bg-base: #060913;
    --bg-surface: rgba(15, 23, 42, 0.78);
    --border: rgba(255, 255, 255, 0.08);
    --border-focus: rgba(59, 130, 246, 0.6);
    --primary: #3b82f6;
    --primary-glow: rgba(59, 130, 246, 0.4);
    --success: #10b981;
    --success-glow: rgba(16, 185, 129, 0.35);
    --warn: #f59e0b;
    --danger: #ef4444;
    --text-high: #f8fafc;
    --text-mid: #94a3b8;
    --text-low: #64748b;
    --radius-sm: 8px;
    --radius-md: 14px;
    --radius-lg: 20px;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; }
  body {
    font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: radial-gradient(circle at 50% 0%, #1e1b4b 0%, var(--bg-base) 65%);
    color: var(--text-high);
    min-height: 100vh;
    padding: clamp(10px, 3vw, 24px);
    display: flex;
    justify-content: center;
    overflow-x: hidden;
  }
  .app-shell { width: 100%; max-width: 1100px; display: flex; flex-direction: column; gap: 18px; }
  
  header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 12px;
    padding: 8px 0 16px;
    border-bottom: 1px solid var(--border);
  }
  .brand { display: flex; align-items: center; gap: 12px; }
  .brand-logo {
    width: 44px; height: 44px; border-radius: var(--radius-md);
    background: linear-gradient(135deg, #3b82f6, #8b5cf6);
    display: flex; align-items: center; justify-content: center;
    box-shadow: 0 0 24px var(--primary-glow);
    flex-shrink: 0;
  }
  .brand-logo svg { width: 24px; height: 24px; fill: white; }
  .brand h1 { font-size: clamp(1.15rem, 2.5vw, 1.45rem); font-weight: 800; letter-spacing: -0.5px; }
  .brand h1 span { color: var(--primary); }
  .brand p { font-size: 0.75rem; color: var(--text-mid); }
  
  .badge {
    padding: 6px 14px; border-radius: 999px; font-size: 0.78rem; font-weight: 700;
    display: inline-flex; align-items: center; gap: 8px;
    background: rgba(16, 185, 129, 0.12); color: var(--success); border: 1px solid var(--success-glow);
    white-space: nowrap;
  }
  .badge.offline {
    background: rgba(239, 68, 68, 0.12); color: var(--danger); border-color: rgba(239, 68, 68, 0.3);
  }
  .pulse-dot { width: 8px; height: 8px; border-radius: 50%; background: currentColor; animation: pulse 1.8s infinite; }
  @keyframes pulse { 0%, 100% { opacity: 0.3; transform: scale(0.9); } 50% { opacity: 1; transform: scale(1.1); } }

  .dashboard-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(min(100%, 300px), 1fr));
    gap: 16px;
  }
  .glass-card {
    background: var(--bg-surface);
    backdrop-filter: blur(16px);
    -webkit-backdrop-filter: blur(16px);
    border: 1px solid var(--border);
    border-radius: var(--radius-lg);
    padding: clamp(16px, 2.5vw, 22px);
    box-shadow: 0 10px 30px rgba(0, 0, 0, 0.35);
    display: flex;
    flex-direction: column;
    justify-content: space-between;
  }
  .card-header {
    display: flex; justify-content: space-between; align-items: center;
    font-size: 0.78rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.6px;
    color: var(--text-mid); margin-bottom: 10px;
  }
  .card-metric { font-size: clamp(1.35rem, 3vw, 1.75rem); font-weight: 800; color: #fff; margin-bottom: 4px; word-break: break-all; }
  .card-meta { font-size: 0.8rem; color: var(--text-mid); font-family: 'JetBrains Mono', monospace; }
  
  .meter-bar { width: 100%; height: 6px; background: rgba(255, 255, 255, 0.08); border-radius: 999px; margin-top: 14px; overflow: hidden; }
  .meter-fill { height: 100%; border-radius: 999px; transition: width 0.4s ease, background 0.4s ease; }
  
  .btn {
    min-height: 42px; padding: 8px 16px; border-radius: var(--radius-md); font-size: 0.85rem; font-weight: 700;
    cursor: pointer; border: none; display: inline-flex; align-items: center; justify-content: center; gap: 8px;
    background: var(--primary); color: white; transition: all 0.2s;
  }
  .btn:hover { background: #2563eb; box-shadow: 0 0 18px var(--primary-glow); }
  .btn-sm { min-height: 36px; padding: 6px 12px; font-size: 0.78rem; border-radius: var(--radius-sm); }
  .btn-ghost { background: rgba(255, 255, 255, 0.06); color: var(--text-high); }
  .btn-ghost:hover { background: rgba(255, 255, 255, 0.12); }
  .btn-success { background: rgba(16, 185, 129, 0.2); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.4); }
  .btn-success:hover { background: var(--success); color: white; }
  .btn-danger { background: rgba(239, 68, 68, 0.18); color: var(--danger); border: 1px solid rgba(239, 68, 68, 0.35); }
  .btn-danger:hover { background: var(--danger); color: white; }

  .content-section {
    background: var(--bg-surface);
    backdrop-filter: blur(16px);
    -webkit-backdrop-filter: blur(16px);
    border: 1px solid var(--border);
    border-radius: var(--radius-lg);
    padding: clamp(16px, 2.5vw, 22px);
  }
  .section-nav {
    display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; flex-wrap: wrap; gap: 10px;
  }
  .section-nav h2 { font-size: 1.05rem; font-weight: 700; letter-spacing: -0.3px; }

  .table-responsive { width: 100%; overflow-x: auto; -webkit-overflow-scrolling: touch; }
  table { width: 100%; border-collapse: collapse; min-width: 540px; }
  th { text-align: left; padding: 10px 12px; font-size: 0.72rem; text-transform: uppercase; color: var(--text-low); border-bottom: 1px solid var(--border); }
  td { padding: 12px 12px; font-size: 0.85rem; border-bottom: 1px solid rgba(255, 255, 255, 0.04); }
  tr:hover { background: rgba(255, 255, 255, 0.02); }

  .signal-pill {
    display: inline-flex; align-items: center; gap: 6px; padding: 4px 10px; border-radius: 999px;
    font-size: 0.75rem; font-family: 'JetBrains Mono', monospace; font-weight: 600;
  }
  .sig-great { background: rgba(16, 185, 129, 0.15); color: #34d399; }
  .sig-good { background: rgba(59, 130, 246, 0.15); color: #60a5fa; }
  .sig-fair { background: rgba(245, 158, 11, 0.15); color: #fbbf24; }
  .sig-weak { background: rgba(239, 68, 68, 0.15); color: #f87171; }

  .form-row { margin-bottom: 14px; }
  .form-row label { display: block; font-size: 0.8rem; font-weight: 600; color: var(--text-mid); margin-bottom: 6px; }
  .form-row input, .form-row select {
    width: 100%; min-height: 44px; background: rgba(0, 0, 0, 0.35); border: 1px solid var(--border);
    border-radius: var(--radius-md); padding: 10px 14px; color: #fff; font-size: 16px; font-family: inherit;
    transition: border-color 0.2s, box-shadow 0.2s;
  }
  .form-row input:focus { outline: none; border-color: var(--primary); box-shadow: 0 0 14px var(--primary-glow); }

  .modal-backdrop {
    position: fixed; inset: 0; background: rgba(0, 0, 0, 0.82); backdrop-filter: blur(12px);
    display: none; align-items: center; justify-content: center; z-index: 1000; padding: 16px;
  }
  .modal-card {
    background: #0f172a; border: 1px solid var(--border); border-radius: var(--radius-lg);
    padding: clamp(18px, 4vw, 26px); max-width: 440px; width: 100%; box-shadow: 0 25px 50px rgba(0, 0, 0, 0.7);
  }

  .known-card {
    display: flex; justify-content: space-between; align-items: center; padding: 12px 14px;
    background: rgba(0, 0, 0, 0.28); border-radius: var(--radius-md); border: 1px solid rgba(255, 255, 255, 0.05);
    flex-wrap: wrap; gap: 10px;
  }
  .known-info { display: flex; flex-direction: column; gap: 3px; }
  .known-name { font-weight: 700; font-size: 0.92rem; color: #fff; word-break: break-all; }
  .known-badge {
    font-size: 0.72rem; font-family: 'JetBrains Mono', monospace; color: var(--primary);
    background: rgba(59, 130, 246, 0.12); padding: 2px 8px; border-radius: 999px; display: inline-block; width: fit-content;
  }
  .known-actions { display: flex; align-items: center; gap: 8px; }

  @media (max-width: 640px) {
    header { flex-direction: column; align-items: flex-start; gap: 10px; }
    .dashboard-grid { grid-template-columns: 1fr; }
    .section-nav { flex-direction: column; align-items: flex-start; }
    .known-card { flex-direction: column; align-items: flex-start; }
    .known-actions { width: 100%; justify-content: flex-end; margin-top: 6px; }
  }
</style>
</head>
<body>
<div class="app-shell">
  <header>
    <div class="brand">
      <div class="brand-logo">
        <svg viewBox="0 0 24 24"><path d="M12 3C6.95 3 3.15 4.85 0 7.23L12 22 24 7.25C20.85 4.87 17.05 3 12 3zm0 3c3.78 0 6.63 1.25 9.07 2.88L12 19.92 2.93 8.88C5.37 7.25 8.22 6 12 6z"/></svg>
      </div>
      <div>
        <h1>AETHER<span>PULSE</span> EXTREME</h1>
        <p>Ultra-Low Latency Wi-Fi Repeater & Range Extender</p>
      </div>
    </div>
    <div id="liveBadge" class="badge offline">
      <span class="pulse-dot"></span>
      <span id="liveStatus">INITIALIZING...</span>
    </div>
  </header>

  <section class="dashboard-grid">
    <div class="glass-card">
      <div>
        <div class="card-header">
          <span>Upstream Uplink</span>
          <span id="uplinkChannel" style="color:var(--primary);">CH --</span>
        </div>
        <div class="card-metric" id="uplinkSsid">Not Connected</div>
        <div class="card-meta" id="uplinkIp">IP: 0.0.0.0 (GW: 0.0.0.0)</div>
      </div>
      <div>
        <div style="display:flex; justify-content:space-between; font-size:0.8rem; margin-top:14px;">
          <span style="color:var(--text-mid);">Kalman Filtered RSSI</span>
          <span id="uplinkRssi" style="font-weight:700;">-- dBm</span>
        </div>
        <div class="meter-bar"><div class="meter-fill" id="rssiBar" style="width:0%; background:var(--primary);"></div></div>
      </div>
    </div>

    <div class="glass-card">
      <div>
        <div class="card-header">
          <span>Repeater Broadcast</span>
          <span id="apIp" style="font-family:'JetBrains Mono',monospace;">192.168.4.1</span>
        </div>
        <div class="card-metric" id="apSsid">AetherPulse-Extender</div>
        <div class="card-meta">DTIM: 1 • Beacon: 103 TU (Zero-Jitter)</div>
      </div>
      <div style="display:flex; justify-content:space-between; align-items:center; margin-top:14px; font-size:0.85rem;">
        <span style="color:var(--text-mid);">Connected Clients</span>
        <span id="clientTally" style="font-weight:800; color:var(--primary); font-size:1.15rem;">0 / 8</span>
      </div>
    </div>

    <div class="glass-card">
      <div>
        <div class="card-header">
          <span>L3 Routing & Silicon Engine</span>
          <span style="color:var(--success);">ACTIVE</span>
        </div>
        <div class="card-metric" id="naptState">lwIP NAPT</div>
        <div class="card-meta">MSS: 1412 B • Modem Sleep: OFF</div>
      </div>
      <div style="display:flex; justify-content:space-between; font-size:0.8rem; margin-top:14px; color:var(--text-mid);">
        <span>Free DRAM: <b id="dramFree" style="color:#fff;">--</b></span>
        <span>Uptime: <b id="uptimeClock" style="color:#fff;">--</b></span>
      </div>
    </div>
  </section>

  <section class="content-section">
    <div class="section-nav">
      <div>
        <h2>2.4 GHz Access Points in Range</h2>
        <p style="font-size:0.8rem; color:var(--text-mid);">Real-time RF spectrum scanner with signal quality analysis</p>
      </div>
      <button class="btn btn-sm" onclick="triggerScan()">
        <svg style="width:16px;height:16px;fill:currentColor;" viewBox="0 0 24 24"><path d="M12 4V1L8 5l4 4V6c3.31 0 6 2.69 6 6 0 1.01-.25 1.97-.7 2.8l1.46 1.46C19.54 15.03 20 13.57 20 12c0-4.42-3.58-8-8-8zm0 14c-3.31 0-6-2.69-6-6 0-1.01.25-1.97.7-2.8L5.24 7.74C4.46 8.97 4 10.43 4 12c0 4.42 3.58 8 8 8v3l4-4-4-4v3z"/></svg>
        Scan Spectrum
      </button>
    </div>
    <div class="table-responsive">
      <table>
        <thead>
          <tr>
            <th>Network SSID</th>
            <th>BSSID MAC</th>
            <th>Signal Quality</th>
            <th>Channel</th>
            <th>Security</th>
            <th>Action</th>
          </tr>
        </thead>
        <tbody id="scanTbody">
          <tr><td colspan="6" style="text-align:center; padding:24px; color:var(--text-mid);">Click 'Scan Spectrum' to detect nearby networks...</td></tr>
        </tbody>
      </table>
    </div>
  </section>

  <div style="display:grid; grid-template-columns:repeat(auto-fit, minmax(min(100%, 340px), 1fr)); gap:16px;">
    <section class="content-section">
      <div class="section-nav">
        <div>
          <h2>Stored Failover Networks</h2>
          <p style="font-size:0.8rem; color:var(--text-mid);">Auto-switches to the strongest known network without dropping clients</p>
        </div>
        <button class="btn btn-sm btn-ghost" onclick="openAddModal()">
          <svg style="width:15px;height:15px;fill:currentColor;" viewBox="0 0 24 24"><path d="M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"/></svg>
          Add AP
        </button>
      </div>
      <div id="knownContainer" style="display:flex; flex-direction:column; gap:10px;">
        <p style="color:var(--text-mid); font-size:0.85rem;">Loading stored profiles...</p>
      </div>
    </section>

    <section class="content-section">
      <div class="section-nav">
        <div>
          <h2>Extender RF & Security Controls</h2>
          <p style="font-size:0.8rem; color:var(--text-mid);">Configure your broadcast parameters</p>
        </div>
      </div>
      <form id="settingsForm" onsubmit="submitSettings(event)">
        <div class="form-row">
          <label>Repeater SSID (Broadcast Name)</label>
          <input type="text" id="formApSsid" required>
        </div>
        <div class="form-row">
          <label>Repeater Passphrase (WPA2)</label>
          <input type="password" id="formApPass" minlength="8" required>
        </div>
        <div class="form-row">
          <label>RF Output Power (Recommended: 16.0 - 18.5 dBm)</label>
          <input type="number" id="formTxPower" step="0.5" min="10" max="20.5" required>
        </div>
        <div style="display:flex; justify-content:space-between; align-items:center; margin-top:20px; flex-wrap:wrap; gap:12px;">
          <label style="font-size:0.85rem; color:var(--text-mid); display:flex; align-items:center; gap:8px;">
            <input type="checkbox" id="formAutoRoam" style="width:auto; min-height:auto;"> 1D Kalman Auto-Roam
          </label>
          <button type="submit" class="btn btn-sm">Save & Apply</button>
        </div>
      </form>
    </section>
  </div>
</div>

<!-- Connect Modal -->
<div class="modal-backdrop" id="modalBackdrop">
  <div class="modal-card">
    <h3 style="margin-bottom:14px; font-size:1.15rem;">Connect to Network</h3>
    <div class="form-row">
      <label>Target SSID</label>
      <input type="text" id="targetSsid" readonly style="background:rgba(255,255,255,0.04);">
    </div>
    <div class="form-row">
      <label>WPA / WPA2 / WPA3 Passphrase</label>
      <input type="password" id="targetPass" placeholder="Enter network password (leave blank if Open)">
    </div>
    <div style="display:flex; justify-content:flex-end; gap:10px; margin-top:22px;">
      <button class="btn btn-sm btn-ghost" onclick="closeModal()">Cancel</button>
      <button class="btn btn-sm" onclick="confirmConnect()">Connect Now</button>
    </div>
  </div>
</div>

<!-- Add Known Network Modal -->
<div class="modal-backdrop" id="addModalBackdrop">
  <div class="modal-card">
    <h3 style="margin-bottom:14px; font-size:1.15rem; display:flex; align-items:center; gap:8px;">
      <svg style="width:20px;height:20px;fill:var(--primary);" viewBox="0 0 24 24"><path d="M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"/></svg>
      Add Failover Wi-Fi Network
    </h3>
    <div class="form-row">
      <label>Network SSID (Name)</label>
      <input type="text" id="newNetSsid" placeholder="e.g. MyHomeNetwork" required>
    </div>
    <div class="form-row">
      <label>WPA / WPA2 / WPA3 Passphrase</label>
      <input type="password" id="newNetPass" placeholder="Enter network password (leave blank if Open)">
    </div>
    <div class="form-row">
      <label>Priority Rank (1 to 10 - higher is preferred first)</label>
      <input type="number" id="newNetPrio" min="1" max="10" value="5">
    </div>
    <div style="display:flex; justify-content:flex-end; gap:10px; margin-top:22px;">
      <button class="btn btn-sm btn-ghost" onclick="closeAddModal()">Cancel</button>
      <button class="btn btn-sm" onclick="submitAddKnown()">Save & Store</button>
    </div>
  </div>
</div>

<script>
  function fetchTelemetry() {
    fetch('/api/status')
      .then(res => res.json())
      .then(d => {
        const badge = document.getElementById('liveBadge');
        const statusText = document.getElementById('liveStatus');
        if (d.connected) {
          badge.className = 'badge';
          statusText.textContent = 'ONLINE & ROUTING';
          document.getElementById('uplinkSsid').textContent = d.upstream_ssid;
          document.getElementById('uplinkIp').textContent = `IP: ${d.upstream_ip} (GW: ${d.upstream_gw})`;
          document.getElementById('uplinkChannel').textContent = `CH ${d.upstream_channel}`;
          document.getElementById('uplinkRssi').textContent = `${d.upstream_rssi.toFixed(1)} dBm`;
          
          let pct = Math.min(100, Math.max(10, Math.round((d.upstream_rssi + 100) * 1.66)));
          const bar = document.getElementById('rssiBar');
          bar.style.width = pct + '%';
          bar.style.background = d.upstream_rssi > -65 ? 'var(--success)' : (d.upstream_rssi > -78 ? 'var(--warn)' : 'var(--danger)');
        } else {
          badge.className = 'badge offline';
          statusText.textContent = 'STANDALONE AP MODE';
          document.getElementById('uplinkSsid').textContent = 'Not Connected';
          document.getElementById('uplinkIp').textContent = 'Clients can configure via Web UI or Serial';
          document.getElementById('uplinkChannel').textContent = 'CH --';
          document.getElementById('uplinkRssi').textContent = '-- dBm';
          document.getElementById('rssiBar').style.width = '0%';
        }
        document.getElementById('apSsid').textContent = d.softap_ssid;
        document.getElementById('apIp').textContent = d.softap_ip;
        document.getElementById('clientTally').textContent = `${d.softap_clients} / ${d.softap_max_clients}`;
        document.getElementById('dramFree').textContent = `${Math.round(d.free_heap / 1024)} KB`;
        document.getElementById('uptimeClock').textContent = `${Math.floor(d.uptime / 60)}m ${d.uptime % 60}s`;
      })
      .catch(e => console.error(e));
  }

  function triggerScan() {
    const tbody = document.getElementById('scanTbody');
    tbody.innerHTML = '<tr><td colspan="6" style="text-align:center; padding:24px; color:var(--text-mid);">Scanning 2.4 GHz spectrum...</td></tr>';
    fetch('/api/scan')
      .then(r => r.json())
      .then(nets => {
        tbody.innerHTML = '';
        if (nets.length === 0) {
          tbody.innerHTML = '<tr><td colspan="6" style="text-align:center; padding:24px;">No access points detected.</td></tr>';
          return;
        }
        nets.sort((a,b) => b.rssi - a.rssi);
        nets.forEach(n => {
          const tr = document.createElement('tr');
          let cls = n.rssi > -65 ? 'sig-great' : (n.rssi > -75 ? 'sig-good' : (n.rssi > -85 ? 'sig-fair' : 'sig-weak'));
          tr.innerHTML = `
            <td><b>${escapeHtml(n.ssid)}</b></td>
            <td style="font-family:'JetBrains Mono',monospace; font-size:0.78rem; color:var(--text-low);">${n.bssid}</td>
            <td><span class="signal-pill ${cls}">${n.rssi} dBm</span></td>
            <td>CH ${n.channel}</td>
            <td><span style="font-size:0.75rem; color:var(--text-mid);">${n.auth}</span></td>
            <td><button class="btn btn-sm btn-ghost" onclick="openConnectModal('${escapeAttr(n.ssid)}')">Connect</button></td>
          `;
          tbody.appendChild(tr);
        });
      })
      .catch(() => {
        tbody.innerHTML = '<tr><td colspan="6" style="text-align:center; padding:24px; color:var(--danger);">Scan failed. Please retry.</td></tr>';
      });
  }

  function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
  }

  function escapeAttr(text) {
    return text.replace(/'/g, "\\'").replace(/"/g, '&quot;');
  }

  function openConnectModal(ssid) {
    document.getElementById('targetSsid').value = ssid;
    document.getElementById('targetPass').value = '';
    document.getElementById('modalBackdrop').style.display = 'flex';
    document.getElementById('targetPass').focus();
  }
  function closeModal() {
    document.getElementById('modalBackdrop').style.display = 'none';
  }
  function confirmConnect() {
    const ssid = document.getElementById('targetSsid').value;
    const pass = document.getElementById('targetPass').value;
    closeModal();
    fetch('/api/connect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password: pass })
    }).then(r => r.json()).then(res => {
      alert(res.message);
      setTimeout(fetchTelemetry, 2500);
      loadKnownNets();
    });
  }

  function openAddModal() {
    document.getElementById('newNetSsid').value = '';
    document.getElementById('newNetPass').value = '';
    document.getElementById('newNetPrio').value = '5';
    document.getElementById('addModalBackdrop').style.display = 'flex';
    document.getElementById('newNetSsid').focus();
  }
  function closeAddModal() {
    document.getElementById('addModalBackdrop').style.display = 'none';
  }
  function submitAddKnown() {
    const ssid = document.getElementById('newNetSsid').value.trim();
    const pass = document.getElementById('newNetPass').value;
    const prio = parseInt(document.getElementById('newNetPrio').value) || 5;

    if (!ssid) {
      alert('SSID cannot be empty!');
      return;
    }

    closeAddModal();
    fetch('/api/known/add', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid: ssid, password: pass, priority: prio })
    })
    .then(r => r.json())
    .then(res => {
      loadKnownNets();
      alert(`Network '${ssid}' saved with Priority ${prio}!`);
    })
    .catch(err => {
      alert('Failed to save network: ' + err);
    });
  }

  function loadSettings() {
    fetch('/api/settings').then(r => r.json()).then(s => {
      document.getElementById('formApSsid').value = s.ap_ssid;
      document.getElementById('formApPass').value = s.ap_pass;
      document.getElementById('formTxPower').value = s.tx_power;
      document.getElementById('formAutoRoam').checked = s.auto_roam;
    });
  }

  function submitSettings(e) {
    e.preventDefault();
    fetch('/api/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        ap_ssid: document.getElementById('formApSsid').value,
        ap_pass: document.getElementById('formApPass').value,
        tx_power: parseFloat(document.getElementById('formTxPower').value),
        auto_roam: document.getElementById('formAutoRoam').checked
      })
    }).then(r => r.json()).then(res => alert(res.message));
  }

  function loadKnownNets() {
    fetch('/api/known').then(r => r.json()).then(list => {
      const c = document.getElementById('knownContainer');
      if (list.length === 0) {
        c.innerHTML = '<p style="color:var(--text-mid); font-size:0.85rem;">No saved failover profiles yet.</p>';
        return;
      }
      c.innerHTML = '';
      list.forEach(k => {
        const d = document.createElement('div');
        d.className = 'known-card';
        d.innerHTML = `
          <div class="known-info">
            <div class="known-name">${escapeHtml(k.ssid)}</div>
            <span class="known-badge">Priority Rank: ${k.priority}</span>
          </div>
          <div class="known-actions">
            <button class="btn btn-sm btn-ghost" onclick="quickConnect('${escapeAttr(k.ssid)}')">Connect</button>
            <button class="btn btn-sm btn-danger" onclick="deleteKnownNet('${escapeAttr(k.ssid)}')">Remove</button>
          </div>
        `;
        c.appendChild(d);
      });
    });
  }

  function quickConnect(ssid) {
    if (!confirm(`Attempt immediate connection to '${ssid}'?`)) return;
    fetch('/api/connect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid })
    }).then(r => r.json()).then(res => {
      alert(res.message);
      setTimeout(fetchTelemetry, 2500);
    });
  }

  function deleteKnownNet(ssid) {
    if (!confirm(`Remove '${ssid}' from saved failover pool?`)) return;
    fetch('/api/known/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid })
    }).then(() => loadKnownNets());
  }

  fetchTelemetry();
  loadSettings();
  loadKnownNets();
  setInterval(fetchTelemetry, 2500);
</script>
</body>
</html>
)rawliteral";
