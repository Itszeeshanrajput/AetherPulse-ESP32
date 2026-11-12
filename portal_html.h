#pragma once

const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="theme-color" content="#060913">
<title>AetherPulse Extreme v2.0 Titanium</title>
<style>
  :root {
    --bg-base: #060913;
    --bg-surface: rgba(15, 23, 42, 0.78);
    --bg-card: rgba(30, 41, 59, 0.6);
    --border: rgba(255, 255, 255, 0.09);
    --border-focus: #38bdf8;
    --primary: #0ea5e9;
    --primary-glow: rgba(14, 165, 233, 0.45);
    --accent: #8b5cf6;
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
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
    background: radial-gradient(circle at 50% 0%, #1e1b4b 0%, var(--bg-base) 65%);
    color: var(--text-high);
    min-height: 100vh;
    padding: clamp(10px, 3vw, 24px);
    display: flex;
    justify-content: center;
    overflow-x: hidden;
  }
  .app-shell { width: 100%; max-width: 1100px; display: flex; flex-direction: column; gap: 16px; }

  /* Header */
  header {
    display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap;
    gap: 12px; padding: 6px 0 14px; border-bottom: 1px solid var(--border);
  }
  .brand { display: flex; align-items: center; gap: 12px; }
  .brand-logo {
    width: 44px; height: 44px; border-radius: var(--radius-md);
    background: linear-gradient(135deg, #0ea5e9, #8b5cf6);
    display: flex; align-items: center; justify-content: center;
    box-shadow: 0 0 24px var(--primary-glow); flex-shrink: 0;
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

  /* Navigation Tabs */
  .nav-tabs {
    display: flex; gap: 8px; overflow-x: auto; -webkit-overflow-scrolling: touch;
    padding: 4px; background: rgba(0, 0, 0, 0.28); border-radius: var(--radius-md);
    border: 1px solid var(--border);
  }
  .tab-btn {
    padding: 9px 16px; border-radius: var(--radius-sm); font-size: 0.85rem; font-weight: 600;
    cursor: pointer; border: none; background: transparent; color: var(--text-mid);
    white-space: nowrap; display: inline-flex; align-items: center; gap: 8px;
    transition: all 0.2s ease;
  }
  .tab-btn:hover { color: #fff; background: rgba(255, 255, 255, 0.05); }
  .tab-btn.active {
    background: var(--primary); color: #fff;
    box-shadow: 0 0 16px var(--primary-glow);
  }

  /* Glass Cards & Layout */
  .tab-content { display: none; flex-direction: column; gap: 16px; }
  .tab-content.active { display: flex; }

  .dashboard-grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 300px), 1fr)); gap: 16px;
  }
  .glass-card {
    background: var(--bg-surface); backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px);
    border: 1px solid var(--border); border-radius: var(--radius-lg);
    padding: clamp(16px, 2.5vw, 22px); box-shadow: 0 10px 30px rgba(0, 0, 0, 0.35);
    display: flex; flex-direction: column; justify-content: space-between;
  }
  .card-header {
    display: flex; justify-content: space-between; align-items: center;
    font-size: 0.78rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.6px;
    color: var(--text-mid); margin-bottom: 10px;
  }
  .card-metric { font-size: clamp(1.35rem, 3vw, 1.75rem); font-weight: 800; color: #fff; margin-bottom: 4px; word-break: break-all; }
  .card-meta { font-size: 0.8rem; color: var(--text-mid); font-family: monospace; }

  .meter-bar { width: 100%; height: 6px; background: rgba(255, 255, 255, 0.08); border-radius: 999px; margin-top: 14px; overflow: hidden; }
  .meter-fill { height: 100%; border-radius: 999px; transition: width 0.4s ease, background 0.4s ease; }

  /* Buttons */
  .btn {
    min-height: 42px; padding: 8px 18px; border-radius: var(--radius-md); font-size: 0.85rem; font-weight: 700;
    cursor: pointer; border: none; display: inline-flex; align-items: center; justify-content: center; gap: 8px;
    background: var(--primary); color: white; transition: all 0.2s;
  }
  .btn:hover { background: #0284c7; box-shadow: 0 0 18px var(--primary-glow); }
  .btn-sm { min-height: 36px; padding: 6px 12px; font-size: 0.78rem; border-radius: var(--radius-sm); }
  .btn-ghost { background: rgba(255, 255, 255, 0.06); color: var(--text-high); }
  .btn-ghost:hover { background: rgba(255, 255, 255, 0.12); }
  .btn-success { background: rgba(16, 185, 129, 0.2); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.4); }
  .btn-success:hover { background: var(--success); color: white; }
  .btn-danger { background: rgba(239, 68, 68, 0.18); color: var(--danger); border: 1px solid rgba(239, 68, 68, 0.35); }
  .btn-danger:hover { background: var(--danger); color: white; }

  /* Content Sections */
  .content-section {
    background: var(--bg-surface); backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px);
    border: 1px solid var(--border); border-radius: var(--radius-lg); padding: clamp(16px, 2.5vw, 22px);
  }
  .section-nav {
    display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; flex-wrap: wrap; gap: 10px;
  }
  .section-nav h2 { font-size: 1.05rem; font-weight: 700; letter-spacing: -0.3px; }

  /* Tables */
  .table-responsive { width: 100%; overflow-x: auto; -webkit-overflow-scrolling: touch; }
  table { width: 100%; border-collapse: collapse; min-width: 540px; }
  th { text-align: left; padding: 10px 12px; font-size: 0.72rem; text-transform: uppercase; color: var(--text-low); border-bottom: 1px solid var(--border); }
  td { padding: 12px 12px; font-size: 0.85rem; border-bottom: 1px solid rgba(255, 255, 255, 0.04); }
  tr:hover { background: rgba(255, 255, 255, 0.02); }

  .signal-pill {
    display: inline-flex; align-items: center; gap: 6px; padding: 4px 10px; border-radius: 999px;
    font-size: 0.75rem; font-family: monospace; font-weight: 600;
  }
  .sig-great { background: rgba(16, 185, 129, 0.15); color: #34d399; }
  .sig-good { background: rgba(14, 165, 233, 0.15); color: #38bdf8; }
  .sig-fair { background: rgba(245, 158, 11, 0.15); color: #fbbf24; }
  .sig-weak { background: rgba(239, 68, 68, 0.15); color: #f87171; }

  /* Forms */
  .form-row { margin-bottom: 14px; }
  .form-row label { display: block; font-size: 0.8rem; font-weight: 600; color: var(--text-mid); margin-bottom: 6px; }
  .form-row input, .form-row select {
    width: 100%; min-height: 44px; background: rgba(0, 0, 0, 0.35); border: 1px solid var(--border);
    border-radius: var(--radius-md); padding: 10px 14px; color: #fff; font-size: 15px; font-family: inherit;
    transition: border-color 0.2s, box-shadow 0.2s;
  }
  .form-row input:focus, .form-row select:focus { outline: none; border-color: var(--primary); box-shadow: 0 0 14px var(--primary-glow); }

  /* Modals */
  .modal-backdrop {
    position: fixed; inset: 0; background: rgba(0, 0, 0, 0.82); backdrop-filter: blur(12px);
    display: none; align-items: center; justify-content: center; z-index: 1000; padding: 16px;
  }
  .modal-card {
    background: #0f172a; border: 1px solid var(--border); border-radius: var(--radius-lg);
    padding: clamp(18px, 4vw, 26px); max-width: 440px; width: 100%; box-shadow: 0 25px 50px rgba(0, 0, 0, 0.7);
  }

  /* Speedometer & Canvas */
  .traffic-canvas { width: 100%; height: 90px; border-radius: var(--radius-md); background: rgba(0, 0, 0, 0.35); }

  /* Toast Notification */
  #toast {
    position: fixed; bottom: 20px; right: 20px; z-index: 9999;
    padding: 12px 20px; border-radius: var(--radius-md); background: #0f172a;
    border: 1px solid var(--primary); color: #fff; font-size: 0.85rem; font-weight: 600;
    box-shadow: 0 10px 30px rgba(0,0,0,0.6); display: none; transform: translateY(20px);
    transition: all 0.3s ease;
  }

  @media (max-width: 640px) {
    header { flex-direction: column; align-items: flex-start; gap: 10px; }
    .dashboard-grid { grid-template-columns: 1fr; }
    .section-nav { flex-direction: column; align-items: flex-start; }
  }
</style>
</head>
<body>
<div class="app-shell">
  <!-- Header -->
  <header>
    <div class="brand">
      <div class="brand-logo">
        <svg viewBox="0 0 24 24"><path d="M12 3C6.95 3 3.15 4.85 0 7.23L12 22 24 7.25C20.85 4.87 17.05 3 12 3zm0 3c3.78 0 6.63 1.25 9.07 2.88L12 19.92 2.93 8.88C5.37 7.25 8.22 6 12 6z"/></svg>
      </div>
      <div>
        <h1>AETHER<span>PULSE</span> v2.0 TITANIUM</h1>
        <p>Zero-Jitter Wi-Fi Range Extender & NAPT Router • aetherpulse.local</p>
      </div>
    </div>
    <div id="liveBadge" class="badge offline">
      <span class="pulse-dot"></span>
      <span id="liveStatus">INITIALIZING...</span>
    </div>
  </header>

  <!-- Navigation Tabs -->
  <nav class="nav-tabs">
    <button class="tab-btn active" onclick="switchTab('dashboard')">
      <svg style="width:16px;height:16px;fill:currentColor;" viewBox="0 0 24 24"><path d="M3 13h8V3H3v10zm0 8h8v-6H3v6zm10 0h8V11h-8v10zm0-18v6h8V3h-8z"/></svg>
      Dashboard
    </button>
    <button class="tab-btn" onclick="switchTab('scanner')">
      <svg style="width:16px;height:16px;fill:currentColor;" viewBox="0 0 24 24"><path d="M12 4V1L8 5l4 4V6c3.31 0 6 2.69 6 6 0 1.01-.25 1.97-.7 2.8l1.46 1.46C19.54 15.03 20 13.57 20 12c0-4.42-3.58-8-8-8zm0 14c-3.31 0-6-2.69-6-6 0-1.01.25-1.97.7-2.8L5.24 7.74C4.46 8.97 4 10.43 4 12c0 4.42 3.58 8 8 8v3l4-4-4-4v3z"/></svg>
      Wi-Fi Scanner
    </button>
    <button class="tab-btn" onclick="switchTab('failover')">
      <svg style="width:16px;height:16px;fill:currentColor;" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-1 14H9V8h2v8zm4 0h-2V8h2v8z"/></svg>
      Failover Pool
    </button>
    <button class="tab-btn" onclick="switchTab('portmap')">
      <svg style="width:16px;height:16px;fill:currentColor;" viewBox="0 0 24 24"><path d="M4 6h16v2H4zm0 5h16v2H4zm0 5h16v2H4z"/></svg>
      Port Forwarding
    </button>
    <button class="tab-btn" onclick="switchTab('security')">
      <svg style="width:16px;height:16px;fill:currentColor;" viewBox="0 0 24 24"><path d="M12 1L3 5v6c0 5.55 3.84 10.74 9 12 5.16-1.26 9-6.45 9-12V5l-9-4zm-2 16l-4-4 1.41-1.41L10 14.17l6.59-6.59L18 9l-8 8z"/></svg>
      Security & MAC
    </button>
    <button class="tab-btn" onclick="switchTab('maintenance')">
      <svg style="width:16px;height:16px;fill:currentColor;" viewBox="0 0 24 24"><path d="M19.35 10.04C18.67 6.59 15.64 4 12 4 9.11 4 6.6 5.64 5.35 8.04 2.34 8.36 0 10.91 0 14c0 3.31 2.69 6 6 6h13c2.76 0 5-2.24 5-5 0-2.64-2.05-4.78-4.65-4.96zM14 13v4h-4v-4H7l5-5 5 5h-3z"/></svg>
      OTA & System
    </button>
  </nav>

  <!-- TAB 1: DASHBOARD -->
  <div id="tab-dashboard" class="tab-content active">
    <section class="dashboard-grid">
      <!-- Upstream Uplink Card -->
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

      <!-- Repeater Broadcast Card -->
      <div class="glass-card">
        <div>
          <div class="card-header">
            <span>Repeater SoftAP</span>
            <span id="apIp" style="font-family:monospace;">192.168.4.1</span>
          </div>
          <div class="card-metric" id="apSsid">AetherPulse-Extender</div>
          <div class="card-meta">DTIM: 1 • Beacon: 103 TU (Zero-Jitter)</div>
        </div>
        <div style="display:flex; justify-content:space-between; align-items:center; margin-top:14px; font-size:0.85rem;">
          <span style="color:var(--text-mid);">Connected Clients</span>
          <span id="clientTally" style="font-weight:800; color:var(--primary); font-size:1.15rem;">0 / 8</span>
        </div>
      </div>

      <!-- Silicon & NAPT Card -->
      <div class="glass-card">
        <div>
          <div class="card-header">
            <span>Silicon & L3 Engine</span>
            <span style="color:var(--success);">ACTIVE</span>
          </div>
          <div class="card-metric" id="naptState">lwIP DNAPT</div>
          <div class="card-meta">Watchdog: 15s TWDT • MSS: 1412 B</div>
        </div>
        <div style="display:flex; justify-content:space-between; font-size:0.8rem; margin-top:14px; color:var(--text-mid);">
          <span>Free DRAM: <b id="dramFree" style="color:#fff;">--</b></span>
          <span>Uptime: <b id="uptimeClock" style="color:#fff;">--</b></span>
        </div>
      </div>
    </section>

    <!-- Real-Time Bandwidth & Waveform Graph -->
    <section class="content-section">
      <div class="section-nav">
        <div>
          <h2>Real-Time Network Telemetry & Throughput</h2>
          <p style="font-size:0.8rem; color:var(--text-mid);">Live traffic throughput with zero-overhead lwIP accounting</p>
        </div>
        <div style="display:flex; gap:16px; font-size:0.85rem; font-family:monospace;">
          <span style="color:var(--primary);">Rx: <b id="rxLive">0.0 KB/s</b></span>
          <span style="color:var(--accent);">Tx: <b id="txLive">0.0 KB/s</b></span>
        </div>
      </div>
      <canvas id="trafficCanvas" class="traffic-canvas"></canvas>
    </section>
  </div>

  <!-- TAB 2: WI-FI SCANNER -->
  <div id="tab-scanner" class="tab-content">
    <section class="content-section">
      <div class="section-nav">
        <div>
          <h2>2.4 GHz Access Points in Range</h2>
          <p style="font-size:0.8rem; color:var(--text-mid);">Background RF spectrum scanner with signal quality analysis</p>
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
  </div>

  <!-- TAB 3: FAILOVER POOL -->
  <div id="tab-failover" class="tab-content">
    <section class="content-section">
      <div class="section-nav">
        <div>
          <h2>Stored Priority Failover Networks</h2>
          <p style="font-size:0.8rem; color:var(--text-mid);">Auto-connects to highest priority network in range</p>
        </div>
        <button class="btn btn-sm btn-ghost" onclick="openAddModal()">
          <svg style="width:15px;height:15px;fill:currentColor;" viewBox="0 0 24 24"><path d="M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"/></svg>
          Add Wi-Fi Network
        </button>
      </div>
      <div id="knownContainer" style="display:flex; flex-direction:column; gap:10px;">
        <p style="color:var(--text-mid); font-size:0.85rem;">Loading stored profiles...</p>
      </div>
    </section>
  </div>

  <!-- TAB 4: PORT FORWARDING -->
  <div id="tab-portmap" class="tab-content">
    <section class="content-section">
      <div class="section-nav">
        <div>
          <h2>lwIP DNAPT Port Forwarding</h2>
          <p style="font-size:0.8rem; color:var(--text-mid);">Expose web servers, cameras, or gaming ports on internal clients to the upstream network</p>
        </div>
        <button class="btn btn-sm btn-ghost" onclick="openPortmapModal()">
          <svg style="width:15px;height:15px;fill:currentColor;" viewBox="0 0 24 24"><path d="M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"/></svg>
          Add Port Mapping
        </button>
      </div>
      <div class="table-responsive">
        <table>
          <thead>
            <tr>
              <th>Protocol</th>
              <th>External Port</th>
              <th>Target IP</th>
              <th>Target Port</th>
              <th>Description</th>
              <th>Action</th>
            </tr>
          </thead>
          <tbody id="portmapTbody">
            <tr><td colspan="6" style="text-align:center; padding:24px; color:var(--text-mid);">No port forward rules defined.</td></tr>
          </tbody>
        </table>
      </div>
    </section>
  </div>

  <!-- TAB 5: SECURITY & MAC -->
  <div id="tab-security" class="tab-content">
    <div style="display:grid; grid-template-columns:repeat(auto-fit, minmax(min(100%, 340px), 1fr)); gap:16px;">
      <!-- MAC Address Spoofing -->
      <section class="content-section">
        <div class="section-nav">
          <div>
            <h2>Station MAC Address Cloning</h2>
            <p style="font-size:0.8rem; color:var(--text-mid);">Bypass hotel, dorm, and captive portals by spoofing your device MAC</p>
          </div>
        </div>
        <div class="form-row">
          <label>Station Interface MAC (XX:XX:XX:XX:XX:XX)</label>
          <input type="text" id="customMacInput" placeholder="e.g. 14:EB:08:3A:0E:2C" maxlength="17">
        </div>
        <div style="display:flex; gap:10px; margin-top:16px; flex-wrap:wrap;">
          <button class="btn btn-sm" onclick="applyCustomMac()">Apply Cloned MAC</button>
          <button class="btn btn-sm btn-ghost" onclick="resetFactoryMac()">Restore Default MAC</button>
        </div>
      </section>

      <!-- DNS AdBlocker & RF Power -->
      <section class="content-section">
        <div class="section-nav">
          <div>
            <h2>Hardware DNS Sinkhole (AdBlock)</h2>
            <p style="font-size:0.8rem; color:var(--text-mid);">Blocks ads and trackers directly at the repeater layer</p>
          </div>
        </div>
        <div style="display:flex; justify-content:space-between; align-items:center; padding:12px; background:rgba(0,0,0,0.25); border-radius:var(--radius-md); margin-bottom:14px;">
          <div>
            <div style="font-weight:700;">DNS AdBlocker Sinkhole</div>
            <div style="font-size:0.75rem; color:var(--text-mid);">Total Blocked Queries: <b id="adBlockCount" style="color:var(--success);">0</b></div>
          </div>
          <button id="adBlockBtn" class="btn btn-sm btn-ghost" onclick="toggleAdBlock()">Toggle</button>
        </div>

        <form id="settingsForm" onsubmit="submitSettings(event)">
          <div class="form-row">
            <label>Repeater SoftAP Name</label>
            <input type="text" id="formApSsid" required>
          </div>
          <div class="form-row">
            <label>SoftAP Password (min 8 chars)</label>
            <input type="password" id="formApPass" minlength="8" required>
          </div>
          <div class="form-row">
            <label>RF Transmit Power (dBm, 10 - 20.5)</label>
            <input type="number" id="formTxPower" step="0.5" min="10" max="20.5" required>
          </div>
          <div style="display:flex; justify-content:space-between; align-items:center; margin-top:14px;">
            <label style="font-size:0.85rem; color:var(--text-mid); display:flex; align-items:center; gap:8px;">
              <input type="checkbox" id="formAutoRoam" style="width:auto; min-height:auto;"> 1D Kalman Auto-Roam
            </label>
            <button type="submit" class="btn btn-sm">Save Parameters</button>
          </div>
        </form>
      </section>
    </div>
  </div>

  <!-- TAB 6: MAINTENANCE & OTA -->
  <div id="tab-maintenance" class="tab-content">
    <section class="content-section">
      <div class="section-nav">
        <div>
          <h2>Over-The-Air (OTA) Firmware Flasher</h2>
          <p style="font-size:0.8rem; color:var(--text-mid);">Upload a compiled `.bin` file to update your ESP32 wirelessly</p>
        </div>
      </div>
      <div style="border: 2px dashed var(--border); padding: 30px; border-radius: var(--radius-lg); text-align: center; background: rgba(0,0,0,0.2);">
        <input type="file" id="otaFileInput" accept=".bin" style="display:none;" onchange="handleFileSelected(this)">
        <p style="margin-bottom:14px; font-weight:600;">Drag and drop firmware (.bin) file here or</p>
        <button class="btn btn-sm btn-ghost" onclick="document.getElementById('otaFileInput').click()">Select .bin File</button>
        <p id="otaFileName" style="margin-top:12px; font-size:0.85rem; color:var(--primary); font-family:monospace;"></p>
        <div id="otaProgressWrapper" style="display:none; margin-top:20px;">
          <div class="meter-bar"><div class="meter-fill" id="otaProgressBar" style="width:0%; background:var(--primary);"></div></div>
          <p id="otaProgressText" style="margin-top:8px; font-size:0.8rem; color:var(--text-mid);">0% Uploaded</p>
        </div>
        <div style="margin-top:20px;">
          <button id="otaStartBtn" class="btn btn-sm btn-success" style="display:none;" onclick="startOtaUpload()">Flash Firmware Now</button>
        </div>
      </div>
      <div style="margin-top:24px; display:flex; gap:12px; justify-content:flex-end;">
        <button class="btn btn-sm btn-danger" onclick="rebootDevice()">Reboot Repeater</button>
      </div>
    </section>
  </div>
</div>

<!-- Connect Modal -->
<div class="modal-backdrop" id="modalBackdrop">
  <div class="modal-card">
    <h3 style="margin-bottom:14px; font-size:1.15rem;">Connect to Wi-Fi</h3>
    <div class="form-row">
      <label>Target SSID</label>
      <input type="text" id="targetSsid" readonly style="background:rgba(255,255,255,0.04);">
    </div>
    <div class="form-row">
      <label>WPA / WPA2 / WPA3 Passphrase</label>
      <input type="password" id="targetPass" placeholder="Enter password (leave blank if Open)">
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
    <h3 style="margin-bottom:14px; font-size:1.15rem;">Add Priority Wi-Fi Network</h3>
    <div class="form-row">
      <label>Network SSID</label>
      <input type="text" id="newNetSsid" placeholder="e.g. HomeNetwork" required>
    </div>
    <div class="form-row">
      <label>Passphrase</label>
      <input type="password" id="newNetPass" placeholder="Enter network password">
    </div>
    <div class="form-row">
      <label>Priority Rank (1 to 10 - higher connects first)</label>
      <input type="number" id="newNetPrio" min="1" max="10" value="5">
    </div>
    <div style="display:flex; justify-content:flex-end; gap:10px; margin-top:22px;">
      <button class="btn btn-sm btn-ghost" onclick="closeAddModal()">Cancel</button>
      <button class="btn btn-sm" onclick="submitAddKnown()">Save to Pool</button>
    </div>
  </div>
</div>

<!-- Add Port Forward Modal -->
<div class="modal-backdrop" id="portmapModalBackdrop">
  <div class="modal-card">
    <h3 style="margin-bottom:14px; font-size:1.15rem;">New Port Forward Rule</h3>
    <div class="form-row">
      <label>Protocol</label>
      <select id="pmProto">
        <option value="6">TCP</option>
        <option value="17">UDP</option>
      </select>
    </div>
    <div class="form-row">
      <label>External Port (e.g. 8080)</label>
      <input type="number" id="pmExtPort" min="1" max="65535" required>
    </div>
    <div class="form-row">
      <label>Target Client IP (e.g. 192.168.4.2)</label>
      <input type="text" id="pmIntIp" placeholder="192.168.4.2" required>
    </div>
    <div class="form-row">
      <label>Target Port (e.g. 80)</label>
      <input type="number" id="pmIntPort" min="1" max="65535" required>
    </div>
    <div class="form-row">
      <label>Description / Device</label>
      <input type="text" id="pmDesc" placeholder="e.g. Web Dashboard">
    </div>
    <div style="display:flex; justify-content:flex-end; gap:10px; margin-top:22px;">
      <button class="btn btn-sm btn-ghost" onclick="closePortmapModal()">Cancel</button>
      <button class="btn btn-sm" onclick="submitPortmap()">Save Rule</button>
    </div>
  </div>
</div>

<!-- Toast -->
<div id="toast">Notification</div>

<script>
  let activeTab = 'dashboard';
  const trafficHistory = [];
  const MAX_HISTORY = 40;

  function showToast(msg) {
    const t = document.getElementById('toast');
    t.textContent = msg;
    t.style.display = 'block';
    t.style.transform = 'translateY(0)';
    setTimeout(() => {
      t.style.transform = 'translateY(20px)';
      setTimeout(() => t.style.display = 'none', 300);
    }, 3000);
  }

  function switchTab(name) {
    activeTab = name;
    document.querySelectorAll('.tab-btn').forEach((b, i) => {
      b.classList.toggle('active', b.textContent.toLowerCase().includes(name));
    });
    document.querySelectorAll('.tab-content').forEach(c => {
      c.classList.toggle('active', c.id === 'tab-' + name);
    });
    if (name === 'failover') loadKnownNets();
    if (name === 'portmap') loadPortmap();
    if (name === 'security') loadSettings();
  }

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
        
        // Bandwidth
        const rx = d.rx_rate || 0;
        const tx = d.tx_rate || 0;
        document.getElementById('rxLive').textContent = (rx >= 1024 ? (rx/1024).toFixed(2) + ' MB/s' : rx.toFixed(1) + ' KB/s');
        document.getElementById('txLive').textContent = (tx >= 1024 ? (tx/1024).toFixed(2) + ' MB/s' : tx.toFixed(1) + ' KB/s');
        
        // AdBlock
        document.getElementById('adBlockCount').textContent = d.adblocks_total || 0;
        const adBtn = document.getElementById('adBlockBtn');
        if (d.adblock_enabled) {
          adBtn.textContent = 'Active (Enabled)';
          adBtn.className = 'btn btn-sm btn-success';
        } else {
          adBtn.textContent = 'Disabled';
          adBtn.className = 'btn btn-sm btn-ghost';
        }

        if (d.cloned_mac && d.cloned_mac.length > 0) {
          document.getElementById('customMacInput').value = d.cloned_mac;
        }

        trafficHistory.push({ rx, tx });
        if (trafficHistory.length > MAX_HISTORY) trafficHistory.shift();
        drawTrafficGraph();
      })
      .catch(e => console.error(e));
  }

  function drawTrafficGraph() {
    const canvas = document.getElementById('trafficCanvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    canvas.width = canvas.clientWidth;
    canvas.height = canvas.clientHeight;

    const w = canvas.width;
    const h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    if (trafficHistory.length < 2) return;

    let maxVal = 50;
    trafficHistory.forEach(pt => {
      if (pt.rx > maxVal) maxVal = pt.rx;
      if (pt.tx > maxVal) maxVal = pt.tx;
    });

    const step = w / (MAX_HISTORY - 1);

    // Draw Rx (Cyan)
    ctx.beginPath();
    ctx.strokeStyle = '#0ea5e9';
    ctx.lineWidth = 2;
    trafficHistory.forEach((pt, i) => {
      const x = i * step;
      const y = h - (pt.rx / maxVal) * (h - 10) - 5;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();

    // Draw Tx (Purple)
    ctx.beginPath();
    ctx.strokeStyle = '#8b5cf6';
    ctx.lineWidth = 2;
    trafficHistory.forEach((pt, i) => {
      const x = i * step;
      const y = h - (pt.tx / maxVal) * (h - 10) - 5;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
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
            <td style="font-family:monospace; font-size:0.78rem; color:var(--text-low);">${n.bssid}</td>
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
    showToast(`Associating with '${ssid}'...`);
    fetch('/api/connect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password: pass })
    }).then(r => r.json()).then(res => {
      showToast(res.message);
      setTimeout(fetchTelemetry, 2000);
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

    if (!ssid) { showToast('SSID cannot be empty!'); return; }
    closeAddModal();
    fetch('/api/known/add', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password: pass, priority: prio })
    }).then(r => r.json()).then(res => {
      loadKnownNets();
      showToast(`Saved '${ssid}' (Priority ${prio})`);
    });
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
        d.style.cssText = 'display:flex; justify-content:space-between; align-items:center; padding:12px 14px; background:rgba(0,0,0,0.28); border-radius:var(--radius-md); border:1px solid rgba(255,255,255,0.05); flex-wrap:wrap; gap:10px;';
        d.innerHTML = `
          <div>
            <div style="font-weight:700; font-size:0.92rem; color:#fff;">${escapeHtml(k.ssid)}</div>
            <span style="font-size:0.72rem; font-family:monospace; color:var(--primary); background:rgba(14,165,233,0.12); padding:2px 8px; border-radius:999px;">Priority: ${k.priority}</span>
          </div>
          <div style="display:flex; gap:8px;">
            <button class="btn btn-sm btn-ghost" onclick="quickConnect('${escapeAttr(k.ssid)}')">Connect</button>
            <button class="btn btn-sm btn-danger" onclick="deleteKnownNet('${escapeAttr(k.ssid)}')">Remove</button>
          </div>
        `;
        c.appendChild(d);
      });
    });
  }

  function quickConnect(ssid) {
    showToast(`Connecting to '${ssid}'...`);
    fetch('/api/connect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid })
    }).then(r => r.json()).then(res => {
      showToast(res.message);
      setTimeout(fetchTelemetry, 2500);
    });
  }

  function deleteKnownNet(ssid) {
    fetch('/api/known/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid })
    }).then(() => {
      loadKnownNets();
      showToast(`Removed '${ssid}'`);
    });
  }

  // Port Forwarding
  function openPortmapModal() {
    document.getElementById('pmExtPort').value = '';
    document.getElementById('pmIntIp').value = '192.168.4.2';
    document.getElementById('pmIntPort').value = '';
    document.getElementById('pmDesc').value = '';
    document.getElementById('portmapModalBackdrop').style.display = 'flex';
  }
  function closePortmapModal() {
    document.getElementById('portmapModalBackdrop').style.display = 'none';
  }
  function submitPortmap() {
    const proto = parseInt(document.getElementById('pmProto').value);
    const extPort = parseInt(document.getElementById('pmExtPort').value);
    const intIp = document.getElementById('pmIntIp').value.trim();
    const intPort = parseInt(document.getElementById('pmIntPort').value);
    const desc = document.getElementById('pmDesc').value.trim();

    if (!extPort || !intPort || !intIp) { showToast('Please fill all fields'); return; }
    closePortmapModal();
    fetch('/api/portmap/add', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ proto, extPort, intIp, intPort, desc })
    }).then(r => r.json()).then(res => {
      loadPortmap();
      showToast('Port forward rule added!');
    });
  }

  function loadPortmap() {
    fetch('/api/portmap').then(r => r.json()).then(rules => {
      const tbody = document.getElementById('portmapTbody');
      tbody.innerHTML = '';
      if (!rules || rules.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" style="text-align:center; padding:24px; color:var(--text-mid);">No port forward rules defined.</td></tr>';
        return;
      }
      rules.forEach(r => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
          <td><b style="color:var(--primary);">${r.proto === 6 ? 'TCP' : 'UDP'}</b></td>
          <td style="font-family:monospace; font-weight:700;">${r.extPort}</td>
          <td style="font-family:monospace;">${r.intIp}</td>
          <td style="font-family:monospace;">${r.intPort}</td>
          <td>${escapeHtml(r.desc || '-')}</td>
          <td><button class="btn btn-sm btn-danger" onclick="deletePortmap(${r.proto}, ${r.extPort})">Delete</button></td>
        `;
        tbody.appendChild(tr);
      });
    });
  }

  function deletePortmap(proto, extPort) {
    fetch('/api/portmap/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ proto, extPort })
    }).then(() => {
      loadPortmap();
      showToast(`Removed rule on port ${extPort}`);
    });
  }

  // Security & MAC
  function applyCustomMac() {
    const mac = document.getElementById('customMacInput').value.trim();
    if (mac.length !== 17) { showToast('Invalid MAC (use format XX:XX:XX:XX:XX:XX)'); return; }
    fetch('/api/mac/set', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ mac })
    }).then(r => r.json()).then(res => showToast(res.message));
  }
  function resetFactoryMac() {
    fetch('/api/mac/reset', { method: 'POST' })
      .then(r => r.json())
      .then(res => {
        document.getElementById('customMacInput').value = '';
        showToast(res.message);
      });
  }

  function toggleAdBlock() {
    fetch('/api/adblock/toggle', { method: 'POST' })
      .then(r => r.json())
      .then(res => {
        showToast(res.message);
        fetchTelemetry();
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
    }).then(r => r.json()).then(res => showToast(res.message));
  }

  // OTA Flasher
  let selectedOtaFile = null;
  function handleFileSelected(input) {
    if (input.files && input.files[0]) {
      selectedOtaFile = input.files[0];
      document.getElementById('otaFileName').textContent = `Ready: ${selectedOtaFile.name} (${(selectedOtaFile.size/1024).toFixed(1)} KB)`;
      document.getElementById('otaStartBtn').style.display = 'inline-flex';
    }
  }

  function startOtaUpload() {
    if (!selectedOtaFile) return;
    const btn = document.getElementById('otaStartBtn');
    btn.disabled = true;
    document.getElementById('otaProgressWrapper').style.display = 'block';

    const formData = new FormData();
    formData.append('update', selectedOtaFile);

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/firmware/update', true);

    xhr.upload.onprogress = function(e) {
      if (e.lengthComputable) {
        const pct = Math.round((e.loaded / e.total) * 100);
        document.getElementById('otaProgressBar').style.width = pct + '%';
        document.getElementById('otaProgressText').textContent = `${pct}% Uploaded (${Math.round(e.loaded/1024)} KB / ${Math.round(e.total/1024)} KB)`;
      }
    };

    xhr.onload = function() {
      if (xhr.status === 200) {
        document.getElementById('otaProgressText').textContent = 'Update Succeeded! Rebooting repeater in 5s...';
        showToast('Firmware updated successfully! Device is rebooting...');
        setTimeout(() => location.reload(), 6000);
      } else {
        document.getElementById('otaProgressText').textContent = 'Flash Failed: ' + xhr.responseText;
        showToast('OTA Flash Failed!');
        btn.disabled = false;
      }
    };

    xhr.onerror = function() {
      document.getElementById('otaProgressText').textContent = 'Flash Complete! Device is rebooting...';
      showToast('Device is rebooting with new firmware...');
      setTimeout(() => location.reload(), 6000);
    };

    xhr.send(formData);
  }

  function rebootDevice() {
    if (!confirm('Reboot AetherPulse ESP32 repeater now?')) return;
    fetch('/api/reboot', { method: 'POST' }).then(() => {
      showToast('Rebooting device...');
      setTimeout(() => location.reload(), 5000);
    });
  }

  fetchTelemetry();
  loadSettings();
  setInterval(fetchTelemetry, 2000);
</script>
</body>
</html>
)rawliteral";
