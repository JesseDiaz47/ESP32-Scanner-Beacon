#pragma once

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#090d12">
<title>Jesse Scanner</title>
<style>
  :root {
    color-scheme: dark;
    --bg: #090d12;
    --panel: #111821;
    --panel-2: #161f2a;
    --line: #253141;
    --text: #edf5ff;
    --muted: #8290a3;
    --cyan: #55d9ff;
    --green: #62e6a7;
    --amber: #ffc765;
    --red: #ff7b89;
  }
  * { box-sizing: border-box; }
  html, body { margin: 0; min-height: 100%; background: var(--bg); color: var(--text); }
  body {
    font: 14px/1.45 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    padding: max(14px, env(safe-area-inset-top)) 14px max(24px, env(safe-area-inset-bottom));
  }
  .shell { width: 100%; max-width: 760px; margin: 0 auto; }
  header { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
  .brand { display: flex; align-items: center; gap: 11px; min-width: 0; }
  .mark {
    display: grid; place-items: center; flex: 0 0 40px; height: 40px; border-radius: 12px;
    color: #071017; background: linear-gradient(145deg, var(--cyan), var(--green));
    font: 800 13px/1 ui-monospace, SFMono-Regular, Menlo, monospace;
    box-shadow: 0 8px 30px rgba(85,217,255,.15);
  }
  h1 { margin: 0; font-size: 18px; letter-spacing: -.02em; }
  .subtitle { margin: 1px 0 0; color: var(--muted); font-size: 12px; }
  .status {
    display: inline-flex; align-items: center; gap: 6px; flex: 0 0 auto;
    padding: 6px 9px; border: 1px solid var(--line); border-radius: 999px;
    color: var(--green); background: rgba(98,230,167,.06); font-size: 11px;
    text-transform: uppercase; letter-spacing: .08em;
  }
  .status::before { content: ""; width: 7px; height: 7px; border-radius: 50%; background: currentColor; box-shadow: 0 0 12px currentColor; }
  .tabs {
    display: grid; grid-template-columns: repeat(4, 1fr); gap: 5px;
    margin: 17px 0 12px; padding: 4px; border: 1px solid var(--line);
    border-radius: 14px; background: #0d131b;
  }
  .tab, .action {
    min-height: 44px; border: 0; border-radius: 10px; color: var(--muted);
    background: transparent; font: 700 12px/1 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    cursor: pointer; touch-action: manipulation;
  }
  .tab[aria-selected="true"] { color: var(--text); background: var(--panel-2); box-shadow: inset 0 0 0 1px #2b394b; }
  .panel { display: none; }
  .panel.active { display: block; }
  .card { border: 1px solid var(--line); border-radius: 16px; background: var(--panel); overflow: hidden; }
  .card + .card { margin-top: 10px; }
  .card-head { padding: 14px 14px 11px; border-bottom: 1px solid var(--line); }
  .eyebrow { color: var(--cyan); font: 700 10px/1 ui-monospace, SFMono-Regular, Menlo, monospace; letter-spacing: .11em; text-transform: uppercase; }
  h2 { margin: 5px 0 2px; font-size: 16px; }
  .meta { color: var(--muted); font-size: 12px; }
  .metric-row { display: grid; grid-template-columns: repeat(3, 1fr); gap: 1px; background: var(--line); }
  .metric { padding: 11px 12px; background: var(--panel); }
  .metric b { display: block; color: var(--text); font: 750 17px/1.15 ui-monospace, SFMono-Regular, Menlo, monospace; }
  .metric span { color: var(--muted); font-size: 10px; text-transform: uppercase; letter-spacing: .06em; }
  .table-wrap { width: 100%; overflow-x: auto; }
  table { width: 100%; border-collapse: collapse; table-layout: fixed; }
  th, td { padding: 9px 11px; text-align: left; border-bottom: 1px solid #1d2835; vertical-align: middle; }
  tr:last-child td { border-bottom: 0; }
  th { color: var(--muted); font-size: 10px; font-weight: 700; letter-spacing: .06em; text-transform: uppercase; }
  td { font-size: 12px; }
  .network-name, .device-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .mono { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
  .right { text-align: right; }
  .center { text-align: center; }
  .signal { color: var(--cyan); white-space: nowrap; }
  .empty { color: var(--muted); text-align: center; padding: 30px 12px; }
  .channel-wrap { padding: 16px 10px 12px; overflow: hidden; }
  .channel-chart {
    height: 190px; display: flex; align-items: end; gap: 4px;
    padding: 22px 2px 0; border-bottom: 1px solid var(--line);
    background: repeating-linear-gradient(to top, transparent 0, transparent 39px, rgba(130,144,163,.08) 40px);
  }
  .channel { height: 100%; min-width: 0; flex: 1; display: flex; flex-direction: column; justify-content: end; align-items: center; gap: 4px; }
  .channel-count { color: var(--muted); font: 9px/1 ui-monospace, SFMono-Regular, Menlo, monospace; }
  .channel-bar { width: min(18px, 86%); min-height: 3px; border-radius: 4px 4px 1px 1px; background: #253647; transition: height .25s ease; }
  .channel-bar.cool { background: var(--green); }
  .channel-bar.warm { background: var(--amber); }
  .channel-bar.hot { background: var(--red); }
  .channel-label { color: var(--muted); font: 9px/1 ui-monospace, SFMono-Regular, Menlo, monospace; transform: translateY(15px); }
  .legend { display: flex; justify-content: space-between; gap: 8px; margin-top: 24px; color: var(--muted); font-size: 10px; }
  .legend span { display: inline-flex; align-items: center; gap: 5px; }
  .dot { width: 7px; height: 7px; border-radius: 50%; background: currentColor; }
  .control-row { display: flex; align-items: center; gap: 12px; padding: 13px 14px; }
  .action { flex: 0 0 auto; padding: 0 16px; color: #061116; background: linear-gradient(135deg, var(--cyan), #74f0d0); }
  .action.secondary { color: var(--text); background: var(--panel-2); box-shadow: inset 0 0 0 1px #2b394b; }
  .action:disabled { opacity: .45; cursor: default; }
  .control-note { color: var(--muted); font-size: 11px; }
  .device-main { display: flex; flex-direction: column; min-width: 0; }
  .device-address { color: var(--muted); font-size: 9px; letter-spacing: .02em; }
  .company { color: var(--amber); }
  .footnote { margin: 11px 2px 0; color: #667489; font-size: 10px; text-align: center; }
  .tag-input {
    flex: 1 1 auto; min-width: 0; padding: 9px 11px; border: 1px solid var(--line);
    border-radius: 10px; background: #0a1018; color: var(--text); min-height: 44px; font: 600 16px/1 -apple-system, sans-serif;
    outline: none;
  }
  .tag-input:focus { border-color: var(--cyan); box-shadow: 0 0 0 2px rgba(85,217,255,.18); }
  @media (max-width: 430px) {
    body { padding-left: 10px; padding-right: 10px; }
    .tab { font-size: 11px; }
    th, td { padding-left: 9px; padding-right: 9px; }
    .control-row { align-items: flex-start; flex-direction: column; }
    .action { width: 100%; }
    .channel-chart { gap: 2px; }
    .channel-count, .channel-label { font-size: 8px; }
  }
</style>
</head><body>
<div class="shell">
  <header>
    <div class="brand">
      <div class="mark">JS</div>
      <div><h1>Jesse Scanner</h1><p class="subtitle">Local 2.4 GHz field survey</p></div>
    </div>
    <div class="status" id="radioState">Live</div>
  </header>

  <nav class="tabs" role="tablist" aria-label="Scanner views">
    <button class="tab" data-tab="wifi" role="tab" aria-selected="true">Networks</button>
    <button class="tab" data-tab="channels" role="tab" aria-selected="false">Channels</button>
    <button class="tab" data-tab="ble" role="tab" aria-selected="false">Bluetooth</button>
    <button class="tab" data-tab="heatmap" role="tab" aria-selected="false">Heatmap</button>
  </nav>

  <section class="panel active" id="panel-wifi" role="tabpanel">
    <div class="card">
      <div class="card-head"><div class="eyebrow">Wi-Fi survey</div><h2>Nearby networks</h2><div class="meta">Strongest first · refreshes every 3 seconds</div></div>
      <div class="metric-row">
        <div class="metric"><b id="count">0</b><span>Networks</span></div>
        <div class="metric"><b id="strongest">—</b><span>Strongest</span></div>
        <div class="metric"><b id="busyChannel">—</b><span>Busy channel</span></div>
      </div>
      <div class="table-wrap"><table>
        <colgroup><col><col style="width:46px"><col style="width:48px"><col style="width:82px"></colgroup>
        <thead><tr><th>SSID</th><th class="center">CH</th><th class="center">Lock</th><th class="right">RSSI</th></tr></thead>
        <tbody id="rows"><tr><td colspan="4" class="empty">Scanning…</td></tr></tbody>
      </table></div>
    </div>
  </section>

  <section class="panel" id="panel-channels" role="tabpanel">
    <div class="card">
      <div class="card-head"><div class="eyebrow">Channel analyzer</div><h2>2.4 GHz congestion</h2><div class="meta">Bar height &amp; color = network count</div></div>
      <div class="channel-wrap">
        <div class="channel-chart" id="channelBars" aria-label="Wi-Fi networks observed on channels 1 through 14"></div>
        <div class="legend"><span style="color:var(--green)"><i class="dot"></i>Light</span><span style="color:var(--amber)"><i class="dot"></i>Busy</span><span style="color:var(--red)"><i class="dot"></i>Crowded</span></div>
      </div>
    </div>
  </section>

  <section class="panel" id="panel-ble" role="tabpanel">
    <div class="card">
      <div class="card-head"><div class="eyebrow">Bluetooth LE</div><h2>Nearby advertisers</h2><div class="meta">Passive discovery · strongest first</div></div>
      <div class="control-row">
        <button class="action" id="bleScanButton" type="button">Scan Bluetooth for 5s</button>
        <div class="control-note" id="bleStatus">The iBeacon pauses during a scan, then resumes automatically.</div>
      </div>
      <div class="table-wrap"><table>
        <colgroup><col><col style="width:78px"><col style="width:70px"></colgroup>
        <thead><tr><th>Device</th><th class="right">RSSI</th><th class="right">Mfr ID</th></tr></thead>
        <tbody id="bleRows"><tr><td colspan="3" class="empty">Tap scan when you’re ready.</td></tr></tbody>
      </table></div>
    </div>
  </section>

  <section class="panel" id="panel-heatmap" role="tabpanel">
    <div class="card">
      <div class="card-head"><div class="eyebrow">RSSI heatmap</div><h2>Walk-around signal log</h2><div class="meta">Tag a spot · strongest 6 APs snapshotted · 96 samples in RAM</div></div>
      <div class="control-row">
        <input class="tag-input" id="heatmapTag" type="text" placeholder="Tag this spot — porch, kitchen, garage…">
        <button class="action" id="heatmapSnapshot" type="button">Tag & log</button>
      </div>
      <div class="control-row">
        <button class="action secondary" id="heatmapExport" type="button">Download CSV</button>
        <button class="action secondary" id="heatmapClear" type="button">Clear log</button>
        <div class="control-note" id="heatmapStatus">Walk to a spot, give it a name, and tap Tag & log.</div>
      </div>
      <div class="metric-row">
        <div class="metric"><b id="heatmapSamples">0</b><span>Samples</span></div>
        <div class="metric"><b id="heatmapUnique">0</b><span>Unique APs</span></div>
        <div class="metric"><b id="heatmapLatest">—</b><span>Latest tag</span></div>
      </div>
      <div class="table-wrap"><table>
        <colgroup><col style="width:90px"><col><col style="width:46px"><col style="width:70px"><col style="width:60px"></colgroup>
        <thead><tr><th>Tag</th><th>Network</th><th class="center">CH</th><th class="right">RSSI</th><th class="right">Age</th></tr></thead>
        <tbody id="heatmapRows"><tr><td colspan="5" class="empty">No samples yet.</td></tr></tbody>
      </table></div>
    </div>
  </section>

  <div class="footnote">Local-only · no cloud · no traffic injection</div>
</div>
<script>
var latestNetworks = [];
var blePollTimer = 0;
var heatmapPollTimer = 0;
var heatmapSnapshotInFlight = false;

function selectTab(name) {
  document.querySelectorAll('.tab').forEach(function (button) {
    button.setAttribute('aria-selected', button.dataset.tab === name ? 'true' : 'false');
  });
  document.querySelectorAll('.panel').forEach(function (panel) {
    panel.classList.toggle('active', panel.id === 'panel-' + name);
  });
}

document.querySelectorAll('.tab').forEach(function (button) {
  button.addEventListener('click', function () {
    selectTab(button.dataset.tab);
    if (button.dataset.tab === 'ble') pollBle(false);
    if (button.dataset.tab === 'heatmap') pollHeatmap();
  });
});

function emptyRow(body, columns, message) {
  body.textContent = '';
  var tr = body.insertRow();
  var td = tr.insertCell();
  td.colSpan = columns;
  td.className = 'empty';
  td.textContent = message;
}

function signalBars(cell, rssi) {
  var count = Math.max(0, Math.min(5, Math.floor((rssi + 100) / 12)));
  for (var i = 0; i < count; i++) {
    var bar = document.createElement('span');
    bar.style.cssText = 'display:inline-block;width:3px;height:' + (i * 3 + 4) + 'px;margin-right:1px;background:#55d9ff;border-radius:1px';
    cell.appendChild(bar);
  }
  cell.appendChild(document.createTextNode(' ' + rssi));
}

function channelStats(list) {
  var stats = Array.from({ length: 14 }, function () { return { count: 0, strongest: -127 }; });
  list.forEach(function (entry) {
    if (entry.channel < 1 || entry.channel > 14) return;
    var item = stats[entry.channel - 1];
    item.count++;
    item.strongest = Math.max(item.strongest, entry.rssi);
  });
  return stats;
}

function renderChannels(list) {
  var stats = channelStats(list);
  var maxCount = Math.max.apply(null, stats.map(function (item) { return item.count; }).concat([1]));
  var root = document.getElementById('channelBars');
  root.textContent = '';
  var busiest = 0;
  var busiestCount = 0;
  for (var channel = 1; channel <= 14; channel++) {
    var item = stats[channel - 1];
    if (item.count > busiestCount) { busiest = channel; busiestCount = item.count; }
    var column = document.createElement('div');
    column.className = 'channel';
    column.title = 'Channel ' + channel + ': ' + item.count + ' networks, strongest ' + (item.count ? item.strongest + ' dBm' : 'none');
    var count = document.createElement('span');
    count.className = 'channel-count';
    count.textContent = item.count || '·';
    var bar = document.createElement('span');
    var heat = item.count >= 4 ? 'hot' : item.count >= 2 ? 'warm' : item.count ? 'cool' : '';
    bar.className = 'channel-bar ' + heat;
    bar.style.height = (item.count ? 18 + Math.round((item.count / maxCount) * 115) : 3) + 'px';
    var label = document.createElement('span');
    label.className = 'channel-label';
    label.textContent = channel;
    column.appendChild(count); column.appendChild(bar); column.appendChild(label); root.appendChild(column);
  }
  document.getElementById('busyChannel').textContent = busiest || '—';
}

function renderNetworks(list) {
  latestNetworks = list.slice().sort(function (a, b) { return b.rssi - a.rssi; });
  document.getElementById('count').textContent = latestNetworks.length;
  document.getElementById('strongest').textContent = latestNetworks.length ? latestNetworks[0].rssi : '—';
  var body = document.getElementById('rows');
  if (!latestNetworks.length) { emptyRow(body, 4, 'No networks in range'); renderChannels([]); return; }
  body.textContent = '';
  latestNetworks.forEach(function (entry) {
    var tr = body.insertRow();
    var name = tr.insertCell();
    name.className = 'network-name';
    name.textContent = entry.ssid || '(hidden)';
    if (!entry.ssid) name.style.color = '#667489';
    var ch = tr.insertCell(); ch.className = 'center mono'; ch.textContent = entry.channel;
    var lock = tr.insertCell(); lock.className = 'center'; lock.textContent = entry.encrypted ? '●' : '';
    var rssi = tr.insertCell(); rssi.className = 'right mono signal'; signalBars(rssi, entry.rssi);
  });
  renderChannels(latestNetworks);
}

async function tickWifi() {
  try {
    var response = await fetch('/scan.json', { cache: 'no-store' });
    renderNetworks((await response.json()).entries || []);
  } catch (error) {
    // A radio sweep briefly moves away from the AP channel. Keep the last result.
  }
}

function renderBle(data) {
  var button = document.getElementById('bleScanButton');
  var status = document.getElementById('bleStatus');
  var radio = document.getElementById('radioState');
  button.disabled = !!data.scanning;
  button.textContent = data.scanning ? 'Scanning…' : 'Scan Bluetooth for 5s';
  radio.textContent = data.scanning ? 'BLE scan' : 'Live';
  status.textContent = data.scanning
    ? 'Listening now. The page may pause while the shared radio scans.'
    : data.entries.length + ' devices found. The iBeacon is broadcasting again.';
  var list = data.entries.slice().sort(function (a, b) { return b.rssi - a.rssi; });
  var body = document.getElementById('bleRows');
  if (!list.length) { emptyRow(body, 3, data.scanning ? 'Listening…' : 'No BLE devices captured yet'); return; }
  body.textContent = '';
  list.forEach(function (entry) {
    var tr = body.insertRow();
    var device = tr.insertCell();
    var wrap = document.createElement('div'); wrap.className = 'device-main';
    var name = document.createElement('span'); name.className = 'device-name'; name.textContent = entry.name || 'Unnamed device';
    var address = document.createElement('span'); address.className = 'device-address mono'; address.textContent = entry.address;
    wrap.appendChild(name); wrap.appendChild(address); device.appendChild(wrap);
    var rssi = tr.insertCell(); rssi.className = 'right mono signal'; rssi.textContent = entry.rssi;
    var company = tr.insertCell(); company.className = 'right mono company';
    company.textContent = entry.company < 0 ? '—' : '0x' + entry.company.toString(16).toUpperCase().padStart(4, '0');
  });
}

async function pollBle(keepPolling) {
  try {
    var response = await fetch('/ble.json', { cache: 'no-store' });
    var data = await response.json();
    renderBle(data);
    if (data.scanning || keepPolling) {
      clearTimeout(blePollTimer);
      blePollTimer = setTimeout(function () { pollBle(false); }, data.scanning ? 900 : 0);
    }
  } catch (error) {
    if (keepPolling) {
      clearTimeout(blePollTimer);
      blePollTimer = setTimeout(function () { pollBle(true); }, 900);
    }
  }
}

document.getElementById('bleScanButton').addEventListener('click', async function () {
  var button = this;
  button.disabled = true;
  document.getElementById('bleStatus').textContent = 'Starting passive BLE scan…';
  try {
    var response = await fetch('/ble/scan', { method: 'POST' });
    if (!response.ok && response.status !== 202) throw new Error('scan request rejected');
    pollBle(true);
  } catch (error) {
    button.disabled = false;
    document.getElementById('bleStatus').textContent = 'Could not start the scan. Try again.';
  }
});

function renderHeatmap(data) {
  var status = document.getElementById('heatmapStatus');
  var samples = document.getElementById('heatmapSamples');
  var unique = document.getElementById('heatmapUnique');
  var latest = document.getElementById('heatmapLatest');
  samples.textContent = data.entries.length;
  var seen = {};
  var newestTag = '—';
  data.entries.forEach(function (entry) {
    seen[entry.ssid || '(hidden)'] = true;
    if (newestTag === '—') newestTag = entry.tag;
  });
  unique.textContent = Object.keys(seen).length;
  latest.textContent = newestTag;
  status.textContent = data.entries.length
    ? 'Newest first · ' + data.entries.length + ' samples · top of stack is ' + data.entries[0].age + 's old.'
    : 'No samples yet. Walk to a spot, name it, and tap Tag & log.';

  var body = document.getElementById('heatmapRows');
  if (!data.entries.length) { emptyRow(body, 5, 'No samples yet.'); return; }
  body.textContent = '';
  data.entries.forEach(function (entry) {
    var tr = body.insertRow();
    var tag = tr.insertCell(); tag.className = 'mono'; tag.textContent = entry.tag;
    var network = tr.insertCell(); network.className = 'network-name'; network.textContent = entry.ssid || '(hidden)';
    var ch = tr.insertCell(); ch.className = 'center mono'; ch.textContent = entry.channel;
    var rssi = tr.insertCell(); rssi.className = 'right mono signal'; signalBars(rssi, entry.rssi);
    var age = tr.insertCell(); age.className = 'right mono'; age.textContent = entry.age + 's';
  });
}

async function pollHeatmap() {
  try {
    var response = await fetch('/heatmap.json', { cache: 'no-store' });
    renderHeatmap(await response.json());
  } catch (error) {
    // WiFi sweeps knock the AP off-channel; keep the last table.
  }
  clearTimeout(heatmapPollTimer);
  heatmapPollTimer = setTimeout(pollHeatmap, 2000);
}

document.getElementById('heatmapSnapshot').addEventListener('click', async function () {
  if (heatmapSnapshotInFlight) return;
  heatmapSnapshotInFlight = true;
  var button = this;
  button.disabled = true;
  var tagInput = document.getElementById('heatmapTag');
  var tag = (tagInput.value || '').trim() || 'spot';
  try {
    var response = await fetch('/heatmap/scan?tag=' + encodeURIComponent(tag), { method: 'POST' });
    var result = {};
    try { result = await response.json(); } catch (parseError) { result = {}; }
    if (!response.ok || result.accepted !== true) {
      var reason = result.error || 'the board refused the request';
      var wait = result.retry_after_ms
        ? ' Retry in ' + Math.ceil(result.retry_after_ms / 1000) + 's.'
        : '';
      document.getElementById('heatmapStatus').textContent = 'Not logged: ' + reason + '.' + wait;
      return;
    }
    document.getElementById('heatmapStatus').textContent = 'Tagged "' + tag + '". Waiting for the next sweep to finish…';
    setTimeout(pollHeatmap, 1200);
  } catch (error) {
    document.getElementById('heatmapStatus').textContent = 'Not logged: the board did not answer.';
  } finally {
    setTimeout(function () { heatmapSnapshotInFlight = false; button.disabled = false; }, 1500);
  }
});

document.getElementById('heatmapExport').addEventListener('click', function () {
  window.location.href = '/heatmap.csv';
});

document.getElementById('heatmapClear').addEventListener('click', async function () {
  if (!confirm('Clear every heatmap sample from RAM?')) return;
  try {
    await fetch('/heatmap/clear', { method: 'POST' });
    setTimeout(pollHeatmap, 600);
  } catch (error) {
    document.getElementById('heatmapStatus').textContent = 'Clear rejected.';
  }
});

tickWifi();
setInterval(tickWifi, 3000);
setTimeout(pollHeatmap, 1500);
</script>
</body></html>
)rawliteral";
