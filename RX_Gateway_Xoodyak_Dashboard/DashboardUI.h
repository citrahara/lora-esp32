#pragma once
#include <Arduino.h>
#include <pgmspace.h>

// Dashboard UI bergaya meteo/grafana: tiga grafik sejajar horizontal.
// Data diambil dari endpoint /api/data pada receiver ESP32.
// Timestamp X-axis memakai GMT+7/WIB dari waktu browser + uptime ESP32.

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>LoRa Electrical Telemetry Dashboard</title>
  <style>
    :root {
      --bg: #f4f4f4;
      --panel: #ffffff;
      --text: #2f343b;
      --muted: #6b7280;
      --line: #d8dbe0;
      --grid: #d6d6d6;
      --blue: #4285f4;
      --green: #34a853;
      --orange: #f45100;
      --yellow: #d9a300;
      --red: #dc2626;
      --chip: #f8fafc;
    }

    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif;
      background: var(--bg);
      color: var(--text);
    }

    .dashboard { width: 100%; min-height: 100vh; }

    .topbar {
      height: 44px;
      background: #ffffff;
      border-bottom: 1px solid var(--line);
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 0 14px;
      gap: 12px;
    }

    .title-wrap { display: flex; align-items: center; gap: 9px; min-width: 0; }
    .grid-icon {
      width: 16px; height: 16px;
      display: grid; grid-template-columns: repeat(2, 1fr); gap: 2px;
      flex: 0 0 auto;
    }
    .grid-icon span { border: 1.8px solid #4b5563; border-radius: 2px; }
    .title {
      font-size: 18px;
      font-weight: 500;
      color: #2f343b;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .star { color: #4b5563; font-size: 18px; }

    .toolbar { display: flex; align-items: center; gap: 6px; color: #4b5563; }
    .tool-btn, .time-range {
      border: 1px solid var(--line);
      background: #ffffff;
      min-height: 30px;
      padding: 5px 9px;
      border-radius: 2px;
      font-size: 13px;
      color: #374151;
      display: inline-flex;
      align-items: center;
      gap: 6px;
    }
    .live-dot {
      width: 8px; height: 8px; border-radius: 50%;
      background: var(--red);
      display: inline-block;
    }
    .live-dot.ok { background: var(--green); }
    .live-dot.pulse { animation: pulse .45s ease-out; }
    @keyframes pulse { from { box-shadow: 0 0 0 0 rgba(52,168,83,.55); } to { box-shadow: 0 0 0 10px rgba(52,168,83,0); } }

    .filterbar {
      padding: 8px 14px 0;
      display: flex;
      align-items: center;
      gap: 8px;
      flex-wrap: wrap;
    }
    .query-chip {
      border: 1px solid var(--line);
      background: #ffffff;
      color: #2563eb;
      padding: 7px 12px;
      font-size: 13px;
      border-radius: 2px;
      font-weight: 500;
    }
    select {
      border: 1px solid #bfc5cd;
      background: #ffffff;
      padding: 7px 28px 7px 10px;
      font-size: 13px;
      color: #374151;
      border-radius: 2px;
    }
    .status-text { color: var(--muted); font-size: 13px; margin-left: auto; }

    .content { padding: 12px 22px 28px; }

    .chart-grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      column-gap: 28px;
      row-gap: 18px;
      margin-top: 4px;
    }

    .chart-panel {
      background: transparent;
      min-width: 0;
      padding: 4px 0 2px;
    }
    .chart-title {
      text-align: center;
      font-size: 15px;
      font-weight: 650;
      margin: 4px 0 9px;
      color: #30343b;
    }
    .canvas-wrap {
      height: 280px;
      position: relative;
    }
    canvas {
      width: 100%;
      height: 280px;
      display: block;
    }
    .legend {
      min-height: 34px;
      margin-top: 2px;
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      justify-content: flex-start;
      gap: 10px 16px;
      color: #3f4650;
      font-size: 13px;
    }
    .legend-item { display: inline-flex; align-items: center; gap: 6px; white-space: nowrap; }
    .legend-mark { width: 13px; height: 4px; display: inline-block; border-radius: 99px; }

    .summary-panel {
      margin-top: 18px;
      background: #ffffff;
      border: 1px solid var(--line);
      border-radius: 3px;
    }
    .summary-head {
      height: 42px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 0 14px;
      border-bottom: 1px solid var(--line);
      gap: 12px;
    }
    .summary-head h2 { margin: 0; font-size: 16px; font-weight: 650; color: #2f343b; }
    .summary-meta { display: flex; flex-wrap: wrap; gap: 10px 18px; color: var(--muted); font-size: 13px; }
    .summary-meta b { color: #374151; }

    .table-wrap { overflow-x: auto; max-height: 360px; }
    table { width: 100%; border-collapse: collapse; font-size: 13px; background: #ffffff; }
    th, td {
      text-align: left;
      padding: 10px 12px;
      border-bottom: 1px solid #eceff3;
      white-space: nowrap;
    }
    th {
      font-size: 12px;
      color: #687180;
      text-transform: uppercase;
      letter-spacing: .03em;
      font-weight: 700;
      background: #fafafa;
      position: sticky;
      top: 0;
      z-index: 1;
    }
    td { color: #2f343b; font-weight: 500; }
    .muted { color: var(--muted); }
    .mono { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; }
    .payload-row td { color: #4b5563; font-size: 12px; }

    .raw-panel {
      margin-top: 12px;
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
    }
    .raw-box {
      background: #ffffff;
      border: 1px solid var(--line);
      border-radius: 3px;
      min-width: 0;
    }
    .raw-title { padding: 10px 12px; border-bottom: 1px solid var(--line); font-weight: 650; }
    pre {
      margin: 0;
      padding: 12px;
      min-height: 76px;
      overflow: auto;
      white-space: pre-wrap;
      word-break: break-word;
      color: #1f2937;
      font-size: 12px;
      line-height: 1.45;
    }

    @media (max-width: 1050px) {
      .chart-grid { grid-template-columns: 1fr; }
      .canvas-wrap, canvas { height: 300px; }
      .toolbar { display: none; }
      .status-text { margin-left: 0; width: 100%; }
      .raw-panel { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="dashboard">
    <header class="topbar">
      <div class="title-wrap">
        <div class="grid-icon"><span></span><span></span><span></span><span></span></div>
        <div class="title">LoRa Sensor Dashboard / Electrical telemetry monitor</div>
        <div class="star">☆</div>
      </div>
      <div class="toolbar">
        <div class="tool-btn">📊</div>
        <div class="tool-btn">💾</div>
        <div class="time-range" id="timeRange">GMT+7 time range: -</div>
        <div class="tool-btn">⟳</div>
        <div class="tool-btn"><span id="liveDot" class="live-dot"></span><span id="liveText">waiting</span></div>
      </div>
    </header>

    <div class="filterbar">
      <div class="query-chip">query0</div>
      <select id="nodeSelect"><option>Node: All</option></select>
      <select><option>Interval: realtime</option></select>
      <select><option>Timezone: GMT+7 / WIB</option></select>
      <div class="status-text" id="statusText">Dashboard menunggu paket LoRa dari receiver.</div>
    </div>

    <main class="content">
      <section class="chart-grid">
        <div class="chart-panel">
          <div class="chart-title">Tegangan</div>
          <div class="canvas-wrap"><canvas id="voltageChart" width="620" height="320"></canvas></div>
          <div class="legend" id="voltageLegend"></div>
        </div>
        <div class="chart-panel">
          <div class="chart-title">Arus</div>
          <div class="canvas-wrap"><canvas id="currentChart" width="620" height="320"></canvas></div>
          <div class="legend" id="currentLegend"></div>
        </div>
        <div class="chart-panel">
          <div class="chart-title">Daya</div>
          <div class="canvas-wrap"><canvas id="powerChart" width="620" height="320"></canvas></div>
          <div class="legend" id="powerLegend"></div>
        </div>
      </section>

      <section class="summary-panel">
        <div class="summary-head">
          <h2>Summary tiap penerimaan paket</h2>
          <div class="summary-meta">
            <span>Total: <b id="totalPackets">0</b></span>
            <span>Valid: <b id="validPackets">0</b></span>
            <span>Counter TX: <b id="txCounter">-</b></span>
            <span>RSSI/SNR: <b id="radioInfo">-</b></span>
          </div>
        </div>
        <div class="table-wrap">
          <table>
            <thead>
              <tr>
                <th>Timestamp GMT+7</th>
                <th>Node</th>
                <th>Counter</th>
                <th>Tegangan (V)</th>
                <th>Arus (mA)</th>
                <th>Daya (mW)</th>
                <th>RX ms</th>
                <th>RSSI</th>
                <th>SNR</th>
              </tr>
            </thead>
            <tbody id="historyBody">
              <tr><td colspan="9" class="muted">Belum ada data. Receiver masih menunggu paket masuk, seperti manusia menunggu driver ojol yang katanya “sudah dekat”.</td></tr>
            </tbody>
          </table>
        </div>
      </section>

      <section class="raw-panel">
        <div class="raw-box">
          <div class="raw-title">Plaintext hasil dekripsi</div>
          <pre id="plainBox">-</pre>
        </div>
        <div class="raw-box">
          <div class="raw-title">Raw packet LoRa</div>
          <pre id="rawBox">-</pre>
        </div>
      </section>
    </main>
  </div>

  <script>
    const MAX_POINTS = 40;
    const WIB_TZ = 'Asia/Jakarta';
    let lastTotal = -1;
    let demoCounter = 0;
    let demoHistory = [];
    const $ = (id) => document.getElementById(id);

    function fixed(value, digits) {
      const num = Number(value);
      return Number.isFinite(num) ? num.toFixed(digits) : (0).toFixed(digits);
    }

    function formatWib(date, withDate = false) {
      const opt = withDate
        ? { timeZone: WIB_TZ, day: '2-digit', month: '2-digit', year: 'numeric', hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false }
        : { timeZone: WIB_TZ, hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false };
      return new Intl.DateTimeFormat('id-ID', opt).format(date).replaceAll('.', ':');
    }

    function rowsWithTimestamp(data) {
      const uptime = Number(data.uptime_ms || 0);
      const now = Date.now();
      return (data.history || []).slice(-MAX_POINTS).map(row => {
        const rxMs = Number(row.rx_ms || 0);
        const delta = uptime > 0 && rxMs > 0 ? Math.max(0, uptime - rxMs) : 0;
        const rxDate = new Date(now - delta);
        return { ...row, rx_date: rxDate, rx_wib: formatWib(rxDate), rx_wib_full: formatWib(rxDate, true) };
      });
    }

    function resizeCanvas(canvas) {
      const rect = canvas.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      const width = Math.max(320, Math.floor(rect.width * dpr));
      const height = Math.max(220, Math.floor(rect.height * dpr));
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }
      return { w: width, h: height, dpr };
    }

    function niceRange(values) {
      let min = Math.min(...values);
      let max = Math.max(...values);
      if (!Number.isFinite(min) || !Number.isFinite(max)) return { min: 0, max: 1 };
      if (Math.abs(max - min) < 0.000001) {
        const bump = Math.max(Math.abs(max) * 0.1, 1);
        min -= bump; max += bump;
      }
      const pad = (max - min) * 0.12;
      return { min: min - pad, max: max + pad };
    }

    function drawChart(config) {
      const canvas = $(config.canvasId);
      const ctx = canvas.getContext('2d');
      const { w, h, dpr } = resizeCanvas(canvas);
      ctx.clearRect(0, 0, w, h);
      ctx.save();
      ctx.scale(dpr, dpr);

      const cssW = w / dpr;
      const cssH = h / dpr;
      const plot = { left: 58, right: 12, top: 14, bottom: 52 };
      const pw = cssW - plot.left - plot.right;
      const ph = cssH - plot.top - plot.bottom;

      const rows = config.rows || [];
      const data = rows
        .map(r => ({ value: Number(r[config.key]), label: r.rx_wib || '-', full: r.rx_wib_full || '-' }))
        .filter(d => Number.isFinite(d.value));

      ctx.fillStyle = '#f4f4f4';
      ctx.fillRect(0, 0, cssW, cssH);

      ctx.font = '12px system-ui, sans-serif';
      ctx.textBaseline = 'middle';

      if (data.length < 2) {
        ctx.fillStyle = '#6b7280';
        ctx.textAlign = 'center';
        ctx.fillText('Menunggu minimal 2 data paket', cssW / 2, cssH / 2);
        ctx.restore();
        setLegend(config.legendId, config.color, config.node || '-', '-', config.unit);
        return;
      }

      const values = data.map(d => d.value);
      const { min, max } = niceRange(values);
      const x = i => plot.left + (pw * i / (data.length - 1));
      const y = value => plot.top + ph - ((value - min) / (max - min)) * ph;

      ctx.strokeStyle = '#d6d6d6';
      ctx.lineWidth = 1;
      ctx.fillStyle = '#4b5563';
      ctx.textAlign = 'right';
      for (let i = 0; i <= 4; i++) {
        const yy = plot.top + (ph / 4) * i;
        const value = max - ((max - min) / 4) * i;
        ctx.beginPath();
        ctx.moveTo(plot.left, yy);
        ctx.lineTo(plot.left + pw, yy);
        ctx.stroke();
        ctx.fillText(value.toFixed(config.digits), plot.left - 8, yy);
      }

      const tickCount = Math.min(5, data.length);
      ctx.textAlign = 'center';
      ctx.textBaseline = 'top';
      for (let i = 0; i < tickCount; i++) {
        const idx = Math.round((data.length - 1) * i / (tickCount - 1 || 1));
        const xx = x(idx);
        ctx.strokeStyle = '#d6d6d6';
        ctx.beginPath();
        ctx.moveTo(xx, plot.top);
        ctx.lineTo(xx, plot.top + ph);
        ctx.stroke();
        ctx.fillStyle = '#4b5563';
        ctx.fillText(data[idx].label, xx, plot.top + ph + 10);
      }

      ctx.strokeStyle = '#9ca3af';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(plot.left, plot.top);
      ctx.lineTo(plot.left, plot.top + ph);
      ctx.lineTo(plot.left + pw, plot.top + ph);
      ctx.stroke();

      ctx.save();
      ctx.translate(15, plot.top + ph / 2);
      ctx.rotate(-Math.PI / 2);
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillStyle = '#4b5563';
      ctx.fillText(config.unit, 0, 0);
      ctx.restore();

      ctx.strokeStyle = config.color;
      ctx.lineWidth = 2;
      ctx.lineJoin = 'round';
      ctx.lineCap = 'round';
      ctx.beginPath();
      data.forEach((point, idx) => {
        const xx = x(idx);
        const yy = y(point.value);
        if (idx === 0) ctx.moveTo(xx, yy); else ctx.lineTo(xx, yy);
      });
      ctx.stroke();

      ctx.fillStyle = config.color;
      data.forEach((point, idx) => {
        const xx = x(idx);
        const yy = y(point.value);
        ctx.beginPath();
        ctx.arc(xx, yy, idx === data.length - 1 ? 3.4 : 2.4, 0, Math.PI * 2);
        ctx.fill();
      });

      const last = data[data.length - 1];
      setLegend(config.legendId, config.color, config.node || 'Node', last.value.toFixed(config.digits), config.unit);
      ctx.restore();
    }

    function setLegend(id, color, node, value, unit) {
      $(id).innerHTML = `<span class="legend-item"><span class="legend-mark" style="background:${color}"></span>${node} &nbsp; Last: <b>${value}</b> ${unit}</span>`;
    }

    function setLive(ok, pulse) {
      const dot = $('liveDot');
      dot.classList.toggle('ok', ok);
      if (pulse) {
        dot.classList.remove('pulse');
        void dot.offsetWidth;
        dot.classList.add('pulse');
      }
      $('liveText').textContent = ok ? 'Live receiving' : 'waiting';
    }

    function updateNodeOptions(rows, latestNode) {
      const nodes = [...new Set(rows.map(r => r.node).filter(Boolean))];
      $('nodeSelect').innerHTML = `<option>Node: All</option>` + nodes.map(n => `<option>${n}</option>`).join('');
      if (latestNode && nodes.includes(latestNode)) $('nodeSelect').value = latestNode;
    }

    function updateUI(data) {
      const rows = rowsWithTimestamp(data);
      const latest = data.latest || rows[rows.length - 1] || {};
      const latestRow = rows[rows.length - 1] || {};
      const total = Number(data.total_packets || 0);
      const pulse = total !== lastTotal && lastTotal !== -1;
      lastTotal = total;

      const firstTime = rows[0]?.rx_wib_full || '-';
      const lastTime = rows[rows.length - 1]?.rx_wib_full || '-';
      $('timeRange').textContent = `GMT+7: ${firstTime} to ${lastTime}`;
      $('statusText').textContent = total > 0
        ? `Last update ${latestRow.rx_wib_full || '-'} | ${data.status || latest.status || 'Paket valid'}`
        : 'Dashboard menunggu paket LoRa dari receiver.';

      $('totalPackets').textContent = data.total_packets || 0;
      $('validPackets').textContent = data.valid_packets || 0;
      $('txCounter').textContent = latest.counter || '-';
      $('radioInfo').textContent = latest.rssi !== undefined ? `${latest.rssi} / ${fixed(latest.snr, 1)}` : '-';
      $('plainBox').textContent = latest.plaintext || '-';
      $('rawBox').textContent = latest.raw || '-';

      setLive(total > 0, pulse);
      if (rows.length) updateNodeOptions(rows, latest.node);

      const node = latest.node || 'Node';
      drawChart({ canvasId: 'voltageChart', legendId: 'voltageLegend', rows, key: 'voltage', color: '#34a853', unit: 'V', digits: 3, node });
      drawChart({ canvasId: 'currentChart', legendId: 'currentLegend', rows, key: 'current', color: '#f45100', unit: 'mA', digits: 2, node });
      drawChart({ canvasId: 'powerChart', legendId: 'powerLegend', rows, key: 'power', color: '#4285f4', unit: 'mW', digits: 2, node });

      const body = $('historyBody');
      if (!rows.length) {
        body.innerHTML = '<tr><td colspan="9" class="muted">Belum ada data.</td></tr>';
        return;
      }
      body.innerHTML = rows.slice().reverse().map(row => `
        <tr>
          <td>${row.rx_wib_full}</td>
          <td>${row.node || '-'}</td>
          <td>${row.counter || '-'}</td>
          <td>${fixed(row.voltage, 3)}</td>
          <td>${fixed(row.current, 2)}</td>
          <td>${fixed(row.power, 2)}</td>
          <td class="mono">${row.rx_ms ?? '-'}</td>
          <td>${row.rssi ?? '-'}</td>
          <td>${fixed(row.snr, 1)}</td>
        </tr>
      `).join('');
    }

    function makeDemoData() {
      demoCounter++;
      const rxNow = demoCounter * 3000;
      const voltage = 4.8 + Math.sin(demoCounter / 5) * 0.22 + Math.random() * 0.025;
      const current = 120 + Math.sin(demoCounter / 4) * 30 + Math.random() * 8;
      const power = voltage * current;
      const rec = {
        node: 'K1', counter: demoCounter, tx_ms: rxNow - 120, rx_ms: rxNow,
        voltage, current, power,
        rssi: -71 - Math.round(Math.random() * 8), snr: 7.5 + Math.random() * 2,
        plaintext: `dev=K1;ts=${rxNow};v=${voltage.toFixed(3)};i=${current.toFixed(2)};p=${power.toFixed(2)}`,
        raw: 'Preview mode. Buka alamat ESP32 receiver untuk data asli dari LoRa.'
      };
      demoHistory.push(rec);
      demoHistory = demoHistory.slice(-MAX_POINTS);
      return {
        status: 'Preview frontend', total_packets: demoCounter, valid_packets: demoCounter, failed_packets: 0,
        uptime_ms: rxNow, latest: rec, history: demoHistory
      };
    }

    async function fetchData() {
      try {
        const res = await fetch('/api/data?t=' + Date.now(), { cache: 'no-store' });
        if (!res.ok) throw new Error('API tidak siap');
        const data = await res.json();
        updateUI(data);
      } catch (err) {
        if (location.protocol === 'file:') {
          updateUI(makeDemoData());
        } else {
          setLive(false, false);
          $('statusText').textContent = 'API /api/data tidak terbaca dari receiver.';
        }
      }
    }

    window.addEventListener('resize', () => fetchData());
    fetchData();
    setInterval(fetchData, 1000);
  </script>
</body>
</html>

)rawliteral";
