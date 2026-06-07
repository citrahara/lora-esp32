#!/usr/bin/env python3
"""
Dashboard lokal laptop untuk RX LoRa/Xoodyak serial-only.
Tampilan dibuat seperti dashboard meteo/grafana lama, tetapi TIDAK memakai hotspot ESP32.

Alur:
  TX -> LoRa -> RX -> USB Serial -> laptop -> http://127.0.0.1:8000

Cara pakai Windows:
  1. Upload sketch RX_Gateway_Xoodyak_SerialOnly ke board RX.
  2. Tutup Serial Monitor Arduino IDE agar port COM tidak terkunci.
  3. Install pyserial: python -m pip install pyserial
  4. Jalankan: python dashboard_serial_local.py --port COM16 --baud 115200
  5. Buka browser: http://127.0.0.1:8000
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import threading
import time
from collections import deque
from datetime import datetime, timezone, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, Optional

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial belum terinstall.")
    print("Jalankan: python -m pip install pyserial")
    sys.exit(1)

WIB = timezone(timedelta(hours=7))

latest: Dict[str, Any] = {
    "valid": False,
    "status": "Belum ada data dari RX",
    "node": "-",
    "counter": None,
    "timestamp": "-",
    "tx_ms": None,
    "rx_ms": None,
    "voltage": None,
    "current": None,
    "power": None,
    "rssi": None,
    "snr": None,
    "host_time_wib": "-",
    "plaintext": "-",
    "raw": "-",
    "raw_line": "-",
}

# Simpan terbaru di kiri untuk API ringkas, nanti dikirim ke frontend sebagai urutan lama -> baru.
history = deque(maxlen=240)
counts = {
    "total_lines": 0,
    "valid_packets": 0,
    "failed_packets": 0,
    "ignored_lines": 0,
    "serial_connected": False,
    "port": "-",
    "baud": 115200,
    "last_error": "",
    "last_line": "-",
}

lock = threading.Lock()


def now_wib_string() -> str:
    return datetime.now(WIB).strftime("%Y-%m-%d %H:%M:%S")


def to_int(value: str) -> Optional[int]:
    value = value.strip()
    if value in ("-", ""):
        return None
    try:
        return int(value)
    except ValueError:
        return None


def to_float(value: str) -> Optional[float]:
    value = value.strip()
    if value in ("-", ""):
        return None
    try:
        return float(value)
    except ValueError:
        return None


def normalize_timestamp(ts: str) -> str:
    ts = (ts or "-").strip()
    # Dashboard lama enaknya format dd/mm/yyyy, HH:mm:ss di tabel.
    try:
        dt = datetime.strptime(ts, "%Y-%m-%d %H:%M:%S")
        return dt.strftime("%d/%m/%Y, %H:%M:%S")
    except ValueError:
        return ts or "-"


def time_only(ts: str) -> str:
    ts = (ts or "-").strip()
    try:
        dt = datetime.strptime(ts, "%Y-%m-%d %H:%M:%S")
        return dt.strftime("%H:%M:%S")
    except ValueError:
        # kalau sudah format dd/mm/yyyy, HH:mm:ss
        m = re.search(r"(\d{2}:\d{2}:\d{2})", ts)
        return m.group(1) if m else ts


def parse_rx_row(line: str) -> Optional[Dict[str, Any]]:
    """Parse baris tabel RX:
    RX  | K1   |     1 | 2026-06-05 09:13:24 |      3000 | ... | VALID
    """
    stripped = line.strip()
    if not stripped.startswith("RX") or "|" not in stripped:
        return None

    parts = [p.strip() for p in stripped.split("|")]
    if len(parts) < 12:
        return None

    status = " | ".join(parts[11:]).strip() or "-"
    valid = status.upper() == "VALID"

    voltage = to_float(parts[6])
    current = to_float(parts[7])
    power = to_float(parts[8])

    record = {
        "valid": valid,
        "status": status,
        "node": parts[1] or "-",
        "counter": to_int(parts[2]),
        "timestamp": parts[3] or "-",        # yyyy-mm-dd hh:mm:ss dari TX field rt
        "timestamp_display": normalize_timestamp(parts[3] or "-"),
        "time_label": time_only(parts[3] or "-"),
        "tx_ms": to_int(parts[4]),
        "rx_ms": to_int(parts[5]),
        "voltage": voltage,
        "current": current,
        "power": power,
        "rssi": to_int(parts[9]),
        "snr": to_float(parts[10]),
        "host_time_wib": now_wib_string(),
        # Karena RX sengaja cuma print 1 baris rapi, plaintext/raw asli tidak dikirim ke laptop.
        # Diisi ringkasan supaya panel kanan dashboard tetap hidup tanpa membuat Serial Monitor berantakan.
        "plaintext": f"dev={parts[1] or '-'};rt={parts[3] or '-'};ts={parts[4] or '-'};v={voltage if voltage is not None else '-'};i={current if current is not None else '-'};p={power if power is not None else '-'}",
        "raw": stripped,
        "raw_line": stripped,
    }
    return record


def serial_reader(port: str, baud: int) -> None:
    global latest

    while True:
        try:
            with serial.Serial(port, baud, timeout=1) as ser:
                with lock:
                    counts["serial_connected"] = True
                    counts["port"] = port
                    counts["baud"] = baud
                    counts["last_error"] = ""
                print(f"Serial tersambung: {port} @ {baud}")
                print("Buka dashboard: http://127.0.0.1:8000")

                while True:
                    raw = ser.readline()
                    if not raw:
                        continue

                    line = raw.decode("utf-8", errors="replace").strip()
                    if not line:
                        continue

                    print(line)
                    with lock:
                        counts["last_line"] = line

                    record = parse_rx_row(line)
                    if record is None:
                        with lock:
                            counts["ignored_lines"] += 1
                        continue

                    with lock:
                        latest = record
                        history.appendleft(record)
                        counts["total_lines"] += 1
                        if record["valid"]:
                            counts["valid_packets"] += 1
                        else:
                            counts["failed_packets"] += 1

        except serial.SerialException as exc:
            with lock:
                counts["serial_connected"] = False
                counts["last_error"] = str(exc)
            print(f"Serial error: {exc}")
            print("Mencoba konek ulang dalam 3 detik...")
            time.sleep(3)


def build_payload() -> Dict[str, Any]:
    with lock:
        newest_first = list(history)
        oldest_first = list(reversed(newest_first))
        return {
            "latest": dict(latest),
            "history": oldest_first,
            "history_newest_first": newest_first,
            "counts": dict(counts),
            "server_time_wib": now_wib_string(),
            # Kompatibel dengan dashboard style lama
            "status": latest.get("status", "-"),
            "total_packets": counts["total_lines"],
            "valid_packets": counts["valid_packets"],
            "failed_packets": counts["failed_packets"],
            "uptime_ms": latest.get("rx_ms") or 0,
        }


HTML = r"""
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>LoRa Sensor Dashboard Lokal</title>
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
    .grid-icon { width: 16px; height: 16px; display: grid; grid-template-columns: repeat(2, 1fr); gap: 2px; flex: 0 0 auto; }
    .grid-icon span { border: 1.8px solid #4b5563; border-radius: 2px; }
    .title { font-size: 18px; font-weight: 600; color: #2f343b; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
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
    .live-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--red); display: inline-block; }
    .live-dot.ok { background: var(--green); }
    .live-dot.pulse { animation: pulse .45s ease-out; }
    @keyframes pulse { from { box-shadow: 0 0 0 0 rgba(52,168,83,.55); } to { box-shadow: 0 0 0 10px rgba(52,168,83,0); } }
    .filterbar { padding: 8px 14px 0; display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
    .query-chip { border: 1px solid var(--line); background: #ffffff; color: #2563eb; padding: 7px 12px; font-size: 13px; border-radius: 2px; font-weight: 500; }
    select { border: 1px solid #bfc5cd; background: #ffffff; padding: 7px 28px 7px 10px; font-size: 13px; color: #374151; border-radius: 2px; }
    .status-text { color: var(--muted); font-size: 13px; margin-left: auto; }
    .content { padding: 12px 22px 28px; }
    .chart-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); column-gap: 28px; row-gap: 18px; margin-top: 4px; }
    .chart-panel { background: transparent; min-width: 0; padding: 4px 0 2px; }
    .chart-title { text-align: center; font-size: 15px; font-weight: 650; margin: 4px 0 9px; color: #30343b; }
    .canvas-wrap { height: 280px; position: relative; }
    canvas { width: 100%; height: 280px; display: block; }
    .legend { min-height: 34px; margin-top: 2px; display: flex; flex-wrap: wrap; align-items: center; justify-content: flex-start; gap: 10px 16px; color: #3f4650; font-size: 13px; }
    .legend-item { display: inline-flex; align-items: center; gap: 6px; white-space: nowrap; }
    .legend-mark { width: 13px; height: 4px; display: inline-block; border-radius: 99px; }
    .summary-panel { margin-top: 18px; background: #ffffff; border: 1px solid var(--line); border-radius: 3px; }
    .summary-head { min-height: 42px; display: flex; align-items: center; justify-content: space-between; padding: 8px 14px; border-bottom: 1px solid var(--line); gap: 12px; }
    .summary-head h2 { margin: 0; font-size: 16px; font-weight: 650; color: #2f343b; }
    .summary-meta { display: flex; flex-wrap: wrap; gap: 10px 18px; color: var(--muted); font-size: 13px; }
    .summary-meta b { color: #374151; }
    .table-wrap { overflow-x: auto; max-height: 360px; }
    table { width: 100%; border-collapse: collapse; font-size: 13px; background: #ffffff; }
    th, td { text-align: left; padding: 10px 12px; border-bottom: 1px solid #eceff3; white-space: nowrap; }
    th { font-size: 12px; color: #687180; text-transform: uppercase; letter-spacing: .03em; font-weight: 700; background: #fafafa; position: sticky; top: 0; z-index: 1; }
    td { color: #2f343b; font-weight: 500; }
    .muted { color: var(--muted); }
    .mono { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; }
    .raw-panel { margin-top: 12px; display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    .raw-box { background: #ffffff; border: 1px solid var(--line); border-radius: 3px; min-width: 0; }
    .raw-title { padding: 10px 12px; border-bottom: 1px solid var(--line); font-weight: 650; }
    pre { margin: 0; padding: 12px; min-height: 76px; overflow: auto; white-space: pre-wrap; word-break: break-word; color: #1f2937; font-size: 12px; line-height: 1.45; }
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
      <div class="status-text" id="statusText">Dashboard menunggu paket LoRa dari USB Serial RX.</div>
    </div>

    <main class="content">
      <section class="chart-grid">
        <div class="chart-panel">
          <div class="chart-title">Tegangan</div>
          <div class="canvas-wrap"><canvas id="voltageChart"></canvas></div>
          <div class="legend" id="voltageLegend"></div>
        </div>
        <div class="chart-panel">
          <div class="chart-title">Arus</div>
          <div class="canvas-wrap"><canvas id="currentChart"></canvas></div>
          <div class="legend" id="currentLegend"></div>
        </div>
        <div class="chart-panel">
          <div class="chart-title">Daya</div>
          <div class="canvas-wrap"><canvas id="powerChart"></canvas></div>
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
            <span>Serial: <b id="serialInfo">-</b></span>
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
              <tr><td colspan="9" class="muted">Belum ada data. Receiver masih menunggu paket masuk.</td></tr>
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
          <div class="raw-title">Raw packet LoRa / baris serial RX</div>
          <pre id="rawBox">-</pre>
        </div>
      </section>
    </main>
  </div>

  <script>
    const MAX_POINTS = 40;
    let lastTotal = -1;
    const $ = (id) => document.getElementById(id);

    function esc(s) {
      return String(s ?? '-').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
    }

    function fixed(value, digits) {
      const num = Number(value);
      return Number.isFinite(num) ? num.toFixed(digits) : '-';
    }

    function displayTime(row, full = false) {
      if (!row) return '-';
      if (full) return row.timestamp_display || row.timestamp || '-';
      return row.time_label || String(row.timestamp || '-').slice(-8) || '-';
    }

    function rowsForChart(data) {
      return (data.history || []).filter(r => r && r.valid !== false).slice(-MAX_POINTS);
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

    function setLegend(id, color, node, value, unit) {
      $(id).innerHTML = `<span class="legend-item"><span class="legend-mark" style="background:${color}"></span>${esc(node)} &nbsp; Last: <b>${esc(value)}</b> ${esc(unit)}</span>`;
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
        .map(r => ({ value: Number(r[config.key]), label: displayTime(r), full: displayTime(r, true) }))
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
      const nodes = [...new Set(rows.map(r => r.node).filter(Boolean).filter(n => n !== '-'))];
      $('nodeSelect').innerHTML = `<option>Node: All</option>` + nodes.map(n => `<option>${esc(n)}</option>`).join('');
      if (latestNode && nodes.includes(latestNode)) $('nodeSelect').value = latestNode;
    }

    function updateUI(data) {
      const rows = rowsForChart(data);
      const newestRows = (data.history_newest_first || []).filter(r => r && r.valid !== false);
      const latest = data.latest || newestRows[0] || rows[rows.length - 1] || {};
      const counts = data.counts || {};
      const total = Number(data.total_packets || counts.total_lines || 0);
      const pulse = total !== lastTotal && lastTotal !== -1;
      lastTotal = total;

      const firstTime = rows[0] ? displayTime(rows[0], true) : '-';
      const lastTime = rows[rows.length - 1] ? displayTime(rows[rows.length - 1], true) : '-';
      $('timeRange').textContent = `GMT+7: ${firstTime} to ${lastTime}`;

      const connected = !!counts.serial_connected;
      const latestStatus = data.status || latest.status || '-';
      const err = counts.last_error ? ` | ${counts.last_error}` : '';
      $('statusText').textContent = total > 0
        ? `Last update ${displayTime(latest, true)} | ${latestStatus}`
        : (connected ? 'Serial tersambung. Menunggu paket LoRa dari RX.' : 'Serial belum tersambung.' + err);

      $('totalPackets').textContent = total;
      $('validPackets').textContent = data.valid_packets || counts.valid_packets || 0;
      $('txCounter').textContent = latest.counter ?? '-';
      $('radioInfo').textContent = latest.rssi !== null && latest.rssi !== undefined ? `${latest.rssi} / ${fixed(latest.snr, 1)}` : '-';
      $('serialInfo').textContent = connected ? `${counts.port || '-'} @ ${counts.baud || '-'}` : 'OFF';
      $('plainBox').textContent = latest.plaintext || '-';
      $('rawBox').textContent = latest.raw || latest.raw_line || counts.last_line || '-';

      setLive(connected && total > 0, pulse);
      if (rows.length) updateNodeOptions(rows, latest.node);

      const node = latest.node || 'K1';
      drawChart({ canvasId: 'voltageChart', legendId: 'voltageLegend', rows, key: 'voltage', color: '#34a853', unit: 'V', digits: 3, node });
      drawChart({ canvasId: 'currentChart', legendId: 'currentLegend', rows, key: 'current', color: '#f45100', unit: 'mA', digits: 2, node });
      drawChart({ canvasId: 'powerChart', legendId: 'powerLegend', rows, key: 'power', color: '#4285f4', unit: 'mW', digits: 2, node });

      const body = $('historyBody');
      if (!newestRows.length) {
        body.innerHTML = '<tr><td colspan="9" class="muted">Belum ada data. Pastikan COM RX benar dan Serial Monitor Arduino ditutup.</td></tr>';
        return;
      }
      body.innerHTML = newestRows.slice(0, 80).map(row => `
        <tr>
          <td>${esc(displayTime(row, true))}</td>
          <td>${esc(row.node || '-')}</td>
          <td>${esc(row.counter ?? '-')}</td>
          <td>${fixed(row.voltage, 3)}</td>
          <td>${fixed(row.current, 2)}</td>
          <td>${fixed(row.power, 2)}</td>
          <td class="mono">${esc(row.rx_ms ?? '-')}</td>
          <td>${esc(row.rssi ?? '-')}</td>
          <td>${fixed(row.snr, 1)}</td>
        </tr>
      `).join('');
    }

    async function fetchData() {
      try {
        const res = await fetch('/api/data?t=' + Date.now(), { cache: 'no-store' });
        if (!res.ok) throw new Error('API tidak siap');
        const data = await res.json();
        updateUI(data);
      } catch (err) {
        setLive(false, false);
        $('statusText').textContent = 'API /api/data tidak terbaca dari server lokal laptop.';
      }
    }

    window.addEventListener('resize', () => fetchData());
    fetchData();
    setInterval(fetchData, 1000);
  </script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        if self.path in ("/", "/index.html"):
            body = HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if self.path.startswith("/api/data"):
            body = json.dumps(build_payload(), ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_response(404)
        self.end_headers()

    def log_message(self, fmt: str, *args: Any) -> None:
        return


def choose_port(port_arg: Optional[str]) -> str:
    if port_arg:
        return port_arg

    ports = list(list_ports.comports())
    if len(ports) == 1:
        return ports[0].device

    print("Port COM belum dipilih.")
    if ports:
        print("Port yang terdeteksi:")
        for p in ports:
            print(f"  {p.device} - {p.description}")
    print("Jalankan ulang, contoh: python dashboard_serial_local.py --port COM16")
    sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(description="Dashboard lokal laptop style meteo/grafana untuk RX LoRa serial-only")
    parser.add_argument("--port", help="Port serial RX, contoh COM16 atau /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200, help="Baudrate serial, default 115200")
    parser.add_argument("--host", default="127.0.0.1", help="Host dashboard, default 127.0.0.1")
    parser.add_argument("--web-port", type=int, default=8000, help="Port web dashboard, default 8000")
    args = parser.parse_args()

    port = choose_port(args.port)

    t = threading.Thread(target=serial_reader, args=(port, args.baud), daemon=True)
    t.start()

    server = ThreadingHTTPServer((args.host, args.web_port), Handler)
    print(f"Dashboard lokal aktif: http://{args.host}:{args.web_port}")
    print("Tidak ada hotspot ESP32. Browser baca data dari USB Serial RX lewat server lokal laptop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nDashboard dihentikan.")


if __name__ == "__main__":
    main()
