#!/usr/bin/env python3
from __future__ import annotations

import argparse, json, re, socket, sys, threading, time
from collections import deque
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, Optional

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial belum terinstall. Jalankan: python -m pip install pyserial")
    sys.exit(1)

WIB = timezone(timedelta(hours=7))
MAX_HISTORY = 240
lock = threading.Lock()
history = deque(maxlen=MAX_HISTORY)  # urutan lama -> baru

latest: Dict[str, Any] = {
    "valid": False, "status": "Belum ada data dari RX", "node": "-", "counter": None,
    "timestamp": "-", "timestamp_display": "-", "time_label": "-",
    "serial_timestamp": "-", "serial_timestamp_display": "-",
    "tx_ms": None, "rx_ms": None, "voltage": None, "current": None, "power": None,
    "rssi": None, "snr": None, "plaintext": "-", "raw": "-", "raw_line": "-",
    "associated_data": "-", "nonce": "-", "ciphertext": "-", "tag": "-", "encryption_line": "-",
}

counts: Dict[str, Any] = {
    "total_lines": 0, "valid_packets": 0, "failed_packets": 0, "ignored_lines": 0,
    "serial_connected": False, "port": "-", "baud": 115200, "last_error": "", "last_line": "-",
}


def now_wib() -> str:
    return datetime.now(WIB).strftime("%Y-%m-%d %H:%M:%S")


def fmt_ts(ts: str) -> str:
    try:
        return datetime.strptime((ts or "-").strip(), "%Y-%m-%d %H:%M:%S").strftime("%d/%m/%Y, %H:%M:%S")
    except ValueError:
        return ts or "-"


def only_time(ts: str) -> str:
    try:
        return datetime.strptime((ts or "-").strip(), "%Y-%m-%d %H:%M:%S").strftime("%H:%M:%S")
    except ValueError:
        m = re.search(r"(\d{2}:\d{2}:\d{2})", ts or "")
        return m.group(1) if m else (ts or "-")


def to_int(v: str) -> Optional[int]:
    v = (v or "").strip()
    if v in ("", "-"):
        return None
    try:
        return int(v)
    except ValueError:
        return None


def to_float(v: str) -> Optional[float]:
    v = (v or "").strip()
    if v in ("", "-"):
        return None
    try:
        return float(v)
    except ValueError:
        return None


def parse_rx_row(line: str) -> Optional[Dict[str, Any]]:
    # Format:
    # RX | K1 | 212 | 2026-06-11 22:38:48 | 824000 | 20243 | 3.090 | 60.00 | 186.00 | -67 | 9.5 | VALID
    s = line.strip()

    if not s.startswith("RX") or "|" not in s:
        return None

    p = [x.strip() for x in s.split("|")]

    if len(p) < 12:
        return None

    status = " | ".join(p[11:]).strip() or "-"
    laptop_ts = now_wib()
    serial_ts = p[3] or "-"

    node = p[1] or "-"
    ctr = to_int(p[2])
    tx_ms = to_int(p[4])
    rx_ms = to_int(p[5])

    voltage = to_float(p[6])
    current = to_float(p[7])
    power = to_float(p[8])

    return {
        "valid": status.upper() == "VALID",
        "status": status,
        "node": node,
        "counter": ctr,

        # Timestamp utama dashboard memakai jam laptop GMT+7.
        "timestamp": laptop_ts,
        "timestamp_display": fmt_ts(laptop_ts),
        "time_label": only_time(laptop_ts),

        # Timestamp serial tetap disimpan sebagai audit/debug.
        "serial_timestamp": serial_ts,
        "serial_timestamp_display": fmt_ts(serial_ts),

        "tx_ms": tx_ms,
        "rx_ms": rx_ms,
        "voltage": voltage,
        "current": current,
        "power": power,
        "rssi": to_int(p[9]),
        "snr": to_float(p[10]),

        "plaintext": (
            f"dev={node};"
            f"ctr={ctr if ctr is not None else '-'};"
            f"rt_laptop={laptop_ts};"
            f"rt_serial={serial_ts};"
            f"ts={tx_ms if tx_ms is not None else '-'};"
            f"v={voltage if voltage is not None else '-'};"
            f"i={current if current is not None else '-'};"
            f"p={power if power is not None else '-'}"
        ),

        "raw": s,
        "raw_line": s,

        # Diisi setelah baris ENC diterima.
        "associated_data": "-",
        "nonce": "-",
        "ciphertext": "-",
        "tag": "-",
        "encryption_line": "-",
    }


def parse_enc_row(line: str) -> Optional[Dict[str, Any]]:
    # Format wajib:
    # ENC | NODE=K1 | CTR=212 | AD=ver=1;node=K1;ctr=212 | NONCE=... | CT=... | TAG=...
    s = line.strip()

    if not s.startswith("ENC |"):
        return None

    data: Dict[str, str] = {}

    for part in [x.strip() for x in s.split("|")][1:]:
        if "=" not in part:
            continue

        k, v = part.split("=", 1)
        data[k.strip().upper()] = v.strip()

    try:
        ctr = int(data.get("CTR", ""))
    except ValueError:
        ctr = None

    return {
        "node": data.get("NODE", "-"),
        "counter": ctr,
        "associated_data": data.get("AD", "-"),
        "nonce": data.get("NONCE", "-"),
        "ciphertext": data.get("CT", "-"),
        "tag": data.get("TAG", "-"),
        "encryption_line": s,
    }


def attach_enc(enc: Dict[str, Any]) -> None:
    global latest

    target = None
    ctr = enc.get("counter")
    node = enc.get("node")

    if history:
        # Normalnya baris ENC muncul setelah baris RX valid.
        if ctr is None or history[-1].get("counter") == ctr:
            target = history[-1]
        else:
            # Fallback kalau baris ENC telat beberapa baris.
            for item in reversed(history):
                same_ctr = item.get("counter") == ctr
                same_node = node in (None, "-", item.get("node")) or item.get("node") == node

                if same_ctr and same_node:
                    target = item
                    break

    if target is not None:
        target.update(enc)

        if latest.get("counter") == target.get("counter"):
            latest.update(enc)
    else:
        # Biar panel enkripsi tetap menampilkan sesuatu kalau ENC datang tanpa RX.
        latest.update(enc)


def send_time_sync(ser: serial.Serial) -> None:
    ser.write(f"TIME_SYNC={now_wib()}\n".encode("utf-8"))
    ser.flush()


def serial_reader(port: str, baud: int, sync_time: bool) -> None:
    global latest

    while True:
        try:
            with serial.Serial(port, baud, timeout=1, write_timeout=0) as ser:
                with lock:
                    counts.update(serial_connected=True, port=port, baud=baud, last_error="")

                print(f"Serial tersambung: {port} @ {baud}")
                print("Pilih COM RX, bukan COM TX. Komputer belum bisa menebak niat manusia.")

                last_sync = 0.0

                while True:
                    if sync_time and time.time() - last_sync >= 10:
                        try:
                            send_time_sync(ser)
                        except serial.SerialTimeoutException:
                            with lock:
                                counts["last_error"] = "TIME_SYNC timeout, lanjut read-only."

                            sync_time = False

                        last_sync = time.time()

                    raw = ser.readline()

                    if not raw:
                        continue

                    line = raw.decode("utf-8", errors="replace").strip()

                    if not line:
                        continue

                    print(line)

                    with lock:
                        counts["last_line"] = line

                    enc = parse_enc_row(line)

                    if enc is not None:
                        with lock:
                            attach_enc(enc)

                        continue

                    rec = parse_rx_row(line)

                    if rec is None:
                        with lock:
                            counts["ignored_lines"] += 1

                        continue

                    with lock:
                        latest = rec
                        history.append(rec)
                        counts["total_lines"] += 1

                        if rec["valid"]:
                            counts["valid_packets"] += 1
                        else:
                            counts["failed_packets"] += 1

        except serial.SerialException as e:
            with lock:
                counts["serial_connected"] = False
                counts["last_error"] = str(e)

            print(f"Serial error: {e}")
            time.sleep(3)


def payload() -> Dict[str, Any]:
    with lock:
        return {
            "latest": dict(latest),
            "history": [dict(x) for x in history],
            "counts": dict(counts),
            "server_time_wib": now_wib(),
            "status": latest.get("status", "-"),
            "total_packets": counts["total_lines"],
            "valid_packets": counts["valid_packets"],
            "failed_packets": counts["failed_packets"],
            "uptime_ms": latest.get("rx_ms") or 0,
        }


HTML = r'''
<!doctype html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LoRa Xoodyak Dashboard</title>

<style>
:root {
  --bg: #f4f4f4;
  --panel: #fff;
  --text: #2f343b;
  --muted: #6b7280;
  --line: #d8dbe0;
  --green: #16a34a;
  --red: #dc2626;
  --orange: #f97316;
  --blue: #2563eb;
}

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  background: var(--bg);
  color: var(--text);
  font-family: Inter, system-ui, -apple-system, "Segoe UI", Arial, sans-serif;
}

header {
  height: 48px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 0 18px;
  background: white;
  border-bottom: 1px solid var(--line);
}

h1 {
  margin: 0;
  font-size: 18px;
}

.live {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  color: var(--muted);
}

.dot {
  width: 9px;
  height: 9px;
  border-radius: 99px;
  background: var(--red);
  display: inline-block;
}

.dot.ok {
  background: var(--green);
}

.wrap {
  padding: 16px 18px 28px;
}

.notice {
  display: flex;
  flex-wrap: wrap;
  gap: 10px 20px;
  align-items: center;
  background: white;
  border: 1px solid var(--line);
  padding: 10px 12px;
  border-radius: 4px;
  font-size: 13px;
  color: var(--muted);
  margin-bottom: 14px;
}

.notice b {
  color: var(--text);
}

.charts {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 16px;
  margin-bottom: 16px;
}

.card,
.summary {
  background: white;
  border: 1px solid var(--line);
  border-radius: 4px;
  min-width: 0;
}

.card-head,
.summary-head {
  padding: 10px 12px;
  border-bottom: 1px solid var(--line);
  font-weight: 700;
  display: flex;
  justify-content: space-between;
  gap: 12px;
  flex-wrap: wrap;
}

.last,
.muted {
  color: var(--muted);
}

canvas {
  display: block;
  width: 100%;
  height: 230px;
}

.summary-head h2 {
  margin: 0;
  font-size: 16px;
}

.meta {
  display: flex;
  gap: 14px;
  flex-wrap: wrap;
  font-size: 13px;
  color: var(--muted);
}

.meta b {
  color: var(--text);
}

.table-wrap {
  overflow: auto;
  max-height: 420px;
}

table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
}

th,
td {
  padding: 9px 10px;
  border-bottom: 1px solid #eceff3;
  white-space: nowrap;
  text-align: left;
}

th {
  position: sticky;
  top: 0;
  background: #fafafa;
  z-index: 1;
  color: #687180;
  text-transform: uppercase;
  font-size: 11px;
  letter-spacing: .03em;
}

.mono {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}

.hex-cell {
  max-width: 260px;
  overflow: hidden;
  text-overflow: ellipsis;
}

.ok {
  color: var(--green);
  font-weight: 800;
}

.bad {
  color: var(--red);
  font-weight: 800;
}

.raw-grid {
  margin-top: 14px;
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 14px;
}

pre {
  margin: 0;
  padding: 12px;
  min-height: 112px;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-word;
  font-size: 12px;
  line-height: 1.45;
}

@media (max-width: 1100px) {
  header {
    height: auto;
    padding: 12px 18px;
    align-items: flex-start;
    flex-direction: column;
  }

  .charts,
  .raw-grid {
    grid-template-columns: 1fr;
  }

  canvas {
    height: 260px;
  }
}
</style>
</head>

<body>
<header>
  <h1>LoRa Xoodyak Dashboard</h1>
  <div class="live">
    <span id="dot" class="dot"></span>
    <span id="liveText">waiting</span>
  </div>
</header>

<main class="wrap">
  <section class="notice">
    <span>Timestamp utama: <b>Laptop GMT+7/WIB</b></span>
    <span>Serial: <b id="serialInfo">-</b></span>
    <span>Status: <b id="statusInfo">-</b></span>
    <span>Server time: <b id="serverTime">-</b></span>
    <span class="muted">Akses LAN pakai IP laptop penerima, bukan 127.0.0.1 dari perangkat lain.</span>
  </section>

  <section class="charts">
    <div class="card">
      <div class="card-head">
        <span>Tegangan</span>
        <span class="last" id="lastV">- V</span>
      </div>
      <canvas id="chartV"></canvas>
    </div>

    <div class="card">
      <div class="card-head">
        <span>Arus</span>
        <span class="last" id="lastI">- mA</span>
      </div>
      <canvas id="chartI"></canvas>
    </div>

    <div class="card">
      <div class="card-head">
        <span>Daya</span>
        <span class="last" id="lastP">- mW</span>
      </div>
      <canvas id="chartP"></canvas>
    </div>
  </section>

  <section class="summary">
    <div class="summary-head">
      <h2>Summary tiap penerimaan paket</h2>
      <div class="meta">
        <span>Total: <b id="total">0</b></span>
        <span>Valid: <b id="valid">0</b></span>
        <span>Invalid: <b id="invalid">0</b></span>
        <span>Counter TX: <b id="ctr">-</b></span>
        <span>RSSI/SNR: <b id="radio">-</b></span>
      </div>
    </div>

    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th>Timestamp Laptop GMT+7</th>
            <th>Timestamp Serial RX/TX</th>
            <th>Node</th>
            <th>Counter</th>
            <th>AD</th>
            <th>Nonce</th>
            <th>Ciphertext</th>
            <th>Tag</th>
            <th>Tegangan (V)</th>
            <th>Arus (mA)</th>
            <th>Daya (mW)</th>
            <th>RX ms</th>
            <th>RSSI</th>
            <th>SNR</th>
            <th>Status</th>
          </tr>
        </thead>
        <tbody id="body">
          <tr>
            <td colspan="15" class="muted">Belum ada data. Pastikan COM RX benar dan Serial Monitor Arduino ditutup.</td>
          </tr>
        </tbody>
      </table>
    </div>
  </section>

  <section class="raw-grid">
    <div class="card">
      <div class="card-head">Plaintext hasil dekripsi</div>
      <pre id="plain">-</pre>
    </div>

    <div class="card">
      <div class="card-head">Hasil enkripsi Xoodyak</div>
      <pre id="enc">-</pre>
    </div>

    <div class="card">
      <div class="card-head">Raw packet / baris serial RX</div>
      <pre id="raw">-</pre>
    </div>
  </section>
</main>

<script>
const $ = id => document.getElementById(id);
const MAX = 40;

function esc(v) {
  return String(v ?? '-').replace(/[&<>"']/g, c => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;'
  }[c]));
}

function fix(v, d) {
  v = Number(v);
  return Number.isFinite(v) ? v.toFixed(d) : '-';
}

function short(v, a = 20, b = 12) {
  v = String(v || '-');

  if (v === '-' || v.length <= a + b + 3) {
    return v;
  }

  return `${v.slice(0, a)}...${v.slice(-b)}`;
}

function validRows(d) {
  return (d.history || []).filter(r => r && r.valid !== false).slice(-MAX);
}

function chart(id, rows, key, unit, dig, color) {
  const c = $(id);
  const x = c.getContext('2d');
  const r = c.getBoundingClientRect();
  const D = devicePixelRatio || 1;

  c.width = Math.max(300, r.width * D);
  c.height = Math.max(200, r.height * D);

  x.clearRect(0, 0, c.width, c.height);
  x.save();
  x.scale(D, D);

  const W = c.width / D;
  const H = c.height / D;
  const p = { l: 54, r: 12, t: 14, b: 38 };
  const PW = W - p.l - p.r;
  const PH = H - p.t - p.b;

  const pts = rows
    .map(z => ({ y: Number(z[key]), t: z.time_label || '-' }))
    .filter(z => Number.isFinite(z.y));

  x.fillStyle = '#fff';
  x.fillRect(0, 0, W, H);
  x.font = '12px system-ui';

  if (pts.length < 2) {
    x.fillStyle = '#6b7280';
    x.textAlign = 'center';
    x.fillText('Menunggu minimal 2 paket valid', W / 2, H / 2);
    x.restore();
    return;
  }

  let mn = Math.min(...pts.map(z => z.y));
  let mx = Math.max(...pts.map(z => z.y));

  if (Math.abs(mx - mn) < 1e-9) {
    let bump = Math.max(Math.abs(mx) * .1, 1);
    mn -= bump;
    mx += bump;
  }

  let pad = (mx - mn) * .12;
  mn -= pad;
  mx += pad;

  const X = i => p.l + PW * i / (pts.length - 1);
  const Y = v => p.t + PH - ((v - mn) / (mx - mn)) * PH;

  x.strokeStyle = '#d6d6d6';
  x.fillStyle = '#4b5563';
  x.textAlign = 'right';
  x.textBaseline = 'middle';

  for (let i = 0; i <= 4; i++) {
    let yy = p.t + PH * i / 4;
    let val = mx - (mx - mn) * i / 4;

    x.beginPath();
    x.moveTo(p.l, yy);
    x.lineTo(p.l + PW, yy);
    x.stroke();
    x.fillText(val.toFixed(dig), p.l - 7, yy);
  }

  x.textAlign = 'center';
  x.textBaseline = 'top';

  let ticks = Math.min(5, pts.length);

  for (let i = 0; i < ticks; i++) {
    let k = Math.round((pts.length - 1) * i / Math.max(1, ticks - 1));
    x.fillText(pts[k].t, X(k), p.t + PH + 9);
  }

  x.strokeStyle = color;
  x.lineWidth = 2;
  x.beginPath();

  pts.forEach((z, i) => {
    if (i) {
      x.lineTo(X(i), Y(z.y));
    } else {
      x.moveTo(X(i), Y(z.y));
    }
  });

  x.stroke();

  x.fillStyle = color;

  pts.forEach((z, i) => {
    x.beginPath();
    x.arc(X(i), Y(z.y), i === pts.length - 1 ? 3.5 : 2.3, 0, Math.PI * 2);
    x.fill();
  });

  x.save();
  x.translate(15, p.t + PH / 2);
  x.rotate(-Math.PI / 2);
  x.fillStyle = '#4b5563';
  x.textAlign = 'center';
  x.fillText(unit, 0, 0);
  x.restore();

  x.restore();
}

function update(d) {
  const c = d.counts || {};
  const rows = d.history || [];
  const l = d.latest || rows[rows.length - 1] || {};

  $('serverTime').textContent = d.server_time_wib || '-';
  $('statusInfo').textContent = d.status || l.status || '-';
  $('serialInfo').textContent = c.serial_connected ? `${c.port || '-'} @ ${c.baud || '-'}` : 'OFF';

  $('total').textContent = d.total_packets || 0;
  $('valid').textContent = d.valid_packets || 0;
  $('invalid').textContent = d.failed_packets || 0;
  $('ctr').textContent = l.counter ?? '-';
  $('radio').textContent = l.rssi != null ? `${l.rssi} / ${fix(l.snr, 1)}` : '-';

  $('lastV').textContent = `${fix(l.voltage, 3)} V`;
  $('lastI').textContent = `${fix(l.current, 2)} mA`;
  $('lastP').textContent = `${fix(l.power, 2)} mW`;

  $('plain').textContent = l.plaintext || '-';

  $('enc').textContent =
    `AD    : ${l.associated_data || '-'}\n` +
    `NONCE : ${l.nonce || '-'}\n` +
    `CT    : ${l.ciphertext || '-'}\n` +
    `TAG   : ${l.tag || '-'}`;

  $('raw').textContent = l.raw || l.raw_line || c.last_line || '-';

  $('dot').classList.toggle('ok', !!c.serial_connected && rows.length > 0);

  $('liveText').textContent = c.serial_connected
    ? (rows.length ? 'Live receiving' : 'Serial connected, waiting packets')
    : 'Serial disconnected';

  let vr = validRows(d);

  chart('chartV', vr, 'voltage', 'V', 3, '#16a34a');
  chart('chartI', vr, 'current', 'mA', 2, '#f97316');
  chart('chartP', vr, 'power', 'mW', 2, '#2563eb');

  if (!rows.length) {
    $('body').innerHTML = '<tr><td colspan="15" class="muted">Belum ada data. Pastikan COM RX benar dan Serial Monitor Arduino ditutup.</td></tr>';
    return;
  }

  $('body').innerHTML = rows.slice(-80).map(r => `
    <tr>
      <td>${esc(r.timestamp_display || r.timestamp || '-')}</td>
      <td>${esc(r.serial_timestamp_display || r.serial_timestamp || '-')}</td>
      <td>${esc(r.node || '-')}</td>
      <td>${esc(r.counter ?? '-')}</td>

      <td class="mono hex-cell" title="${esc(r.associated_data || '-')}">${esc(short(r.associated_data, 20, 12))}</td>
      <td class="mono hex-cell" title="${esc(r.nonce || '-')}">${esc(short(r.nonce, 18, 10))}</td>
      <td class="mono hex-cell" title="${esc(r.ciphertext || '-')}">${esc(short(r.ciphertext, 24, 12))}</td>
      <td class="mono hex-cell" title="${esc(r.tag || '-')}">${esc(short(r.tag, 18, 10))}</td>

      <td>${fix(r.voltage, 3)}</td>
      <td>${fix(r.current, 2)}</td>
      <td>${fix(r.power, 2)}</td>
      <td class="mono">${esc(r.rx_ms ?? '-')}</td>
      <td>${esc(r.rssi ?? '-')}</td>
      <td>${fix(r.snr, 1)}</td>
      <td class="${r.valid ? 'ok' : 'bad'}">${esc(r.status || '-')}</td>
    </tr>
  `).join('');
}

async function poll() {
  try {
    let r = await fetch('/api/data?t=' + Date.now(), { cache: 'no-store' });
    update(await r.json());
  } catch (e) {
    $('dot').classList.remove('ok');
    $('liveText').textContent = 'API disconnected';
    $('statusInfo').textContent = 'API /api/data tidak terbaca';
  }
}

addEventListener('resize', poll);
poll();
setInterval(poll, 1000);
</script>
</body>
</html>
'''


class Handler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        if self.path in ("/", "/index.html"):
            body = HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if self.path.startswith("/api/data"):
            body = json.dumps(payload(), ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if self.path.startswith("/api/health"):
            body = json.dumps({"ok": True, "time_wib": now_wib()}, ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_response(404)
        self.end_headers()

    def do_OPTIONS(self) -> None:
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
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

    print("Jalankan ulang, contoh: python dashboard_serial_local.py --port COM10")
    sys.exit(1)


def lan_ip() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]

            if ip and not ip.startswith("127."):
                return ip
    except OSError:
        pass

    try:
        ip = socket.gethostbyname(socket.gethostname())

        if ip and not ip.startswith("127."):
            return ip
    except OSError:
        pass

    return "127.0.0.1"


def main() -> None:
    ap = argparse.ArgumentParser(description="Dashboard lokal/LAN untuk RX LoRa Xoodyak serial-only")
    ap.add_argument("--port", help="Port serial RX, contoh COM10 atau /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--web-port", type=int, default=8000)
    ap.add_argument("--sync-time", action="store_true", help="Opsional: kirim TIME_SYNC laptop GMT+7 ke RX")
    args = ap.parse_args()

    port = choose_port(args.port)

    threading.Thread(
        target=serial_reader,
        args=(port, args.baud, args.sync_time),
        daemon=True
    ).start()

    server = ThreadingHTTPServer((args.host, args.web_port), Handler)
    ip = lan_ip()

    print("Dashboard aktif.")
    print(f"  Lokal laptop penerima : http://127.0.0.1:{args.web_port}")
    print(f"  LAN satu jaringan     : http://{ip}:{args.web_port}")
    print("Browser membaca data dari USB Serial RX lewat laptop penerima. Pilih COM RX, bukan COM TX.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nDashboard dihentikan.")


if __name__ == "__main__":
    main()