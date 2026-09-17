"""Desktop USB client for the XIAO cam: live view + camera controls,
talking to the firmware over USB CDC serial instead of WiFi. Single file
by design — see docs/superpowers/specs/2026-09-17-usb-gui-prototype-design.md.
"""

import queue
import threading
import time

import serial
import serial.tools.list_ports

SYNC = bytes([0xAA, 0x55])


MAX_FRAME_LEN = 5_000_000


def extract_frame(buf):
    while True:
        i = buf.find(SYNC)
        if i == -1:
            # No sync yet; keep the buffer unchanged for text or next frame data
            return None, buf
        if i > 0:
            del buf[:i]
        if len(buf) < 6:
            return None, buf
        length = int.from_bytes(buf[2:6], "little")
        if length <= 0 or length > MAX_FRAME_LEN:
            # Bogus length: this sync marker is a false positive (e.g.
            # found inside JPEG payload bytes during a mid-stream connect,
            # or a resync after a desync at connect time). Drop just the 2
            # sync bytes and re-scan for a real one, instead of waiting
            # forever for a buffer that will never reach this "length".
            del buf[:2]
            continue
        total = 6 + length
        if len(buf) < total:
            return None, buf
        frame = bytes(buf[6:total])
        del buf[:total]
        return frame, buf


def extract_line(buf):
    nl = buf.find(b"\n")
    if nl == -1:
        return None, buf
    raw = bytes(buf[:nl])
    del buf[:nl + 1]
    return raw.rstrip(b"\r").decode("ascii", errors="replace"), buf


def demux_step(buf):
    """Pull the next complete item (a reply line or a frame) out of buf, in
    actual stream order.

    A line is only recognized as a line if its '\\n' comes before the next
    frame's sync marker — a naive "drain all frames, then drain all lines"
    approach discards any reply line sitting in front of a frame's sync
    bytes, because extract_frame() deletes everything before the sync it
    finds. The `nl < i` check below is also what stops a stray 0x0A byte
    inside JPEG payload data from being mistaken for a line terminator.

    Returns (kind, value, buf) where kind is "line", "frame", or None if
    nothing complete is available yet (value is None in that case).
    """
    i = buf.find(SYNC)
    nl = buf.find(b"\n")
    if nl != -1 and (i == -1 or nl < i):
        line, buf = extract_line(buf)
        if line is None:
            return None, None, buf
        return "line", line, buf
    frame, buf = extract_frame(buf)
    if frame is None:
        return None, None, buf
    return "frame", frame, buf


class UsbTransport:
    def __init__(self, on_frame, on_status=None, on_disconnect=None):
        self._on_frame = on_frame
        self._on_status = on_status
        self._on_disconnect = on_disconnect
        self._port = None
        self._buf = bytearray()
        self._reply_q = queue.Queue()
        self._reader_thread = None
        self._stop = threading.Event()

    def list_ports(self):
        return [p.device for p in serial.tools.list_ports.comports()]

    def connect(self, port_name, baudrate=921600):
        self.disconnect()
        self._port = serial.Serial(port_name, baudrate=baudrate, timeout=0.05)
        self._stop.clear()
        self._reader_thread = threading.Thread(target=self._read_loop, daemon=True)
        self._reader_thread.start()

    def disconnect(self):
        self._stop.set()
        if self._reader_thread:
            self._reader_thread.join(timeout=1.0)
            self._reader_thread = None
        if self._port:
            self._port.close()
            self._port = None
        self._buf.clear()

    def is_connected(self):
        port = self._port
        return port is not None and port.is_open

    def send_command(self, line, timeout=2.0):
        port = self._port
        if port is None or not port.is_open:
            raise RuntimeError("not connected")
        with self._reply_q.mutex:
            self._reply_q.queue.clear()
        port.write((line + "\n").encode("ascii"))
        try:
            return self._reply_q.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError(f"no reply to {line!r}")

    def _read_loop(self):
        while not self._stop.is_set():
            try:
                chunk = self._port.read(4096)
            except (OSError, serial.SerialException):
                # Cable pulled / device reset: stop treating the port as
                # connected so is_connected() and send_command() reflect
                # reality immediately, without waiting on disconnect()'s
                # thread-join (we ARE that thread).
                try:
                    self._port.close()
                except Exception:
                    pass
                self._port = None
                if self._on_disconnect:
                    self._on_disconnect()
                break
            if chunk:
                self._buf.extend(chunk)
            while True:
                kind, value, self._buf = demux_step(self._buf)
                if kind is None:
                    break
                if kind == "line":
                    self._reply_q.put(value)
                    if self._on_status and value.startswith("OK:"):
                        self._on_status(value)
                else:
                    self._on_frame(value)


HTML = """
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>XIAO Cam — USB</title>
<style>
  body { font-family: system-ui, sans-serif; background: #111; color: #eee; text-align: center; padding: 1.5rem; }
  button { font-size: 1rem; padding: 0.5rem 1.5rem; cursor: pointer; margin: 0.25rem; }
  button:disabled { cursor: default; opacity: 0.5; }
  #frame { max-width: 85vw; margin-top: 1rem; border: 1px solid #333; }
  #stats, #message { margin-top: 0.5rem; color: #888; font-size: 0.9rem; }
  .controls { margin-top: 1rem; }
  label { display: inline-block; margin: 0 0.75rem; }
</style>
</head>
<body>
  <h1>XIAO Cam — USB</h1>
  <select id="port"></select>
  <button id="connect">Connect</button>
  <div><img id="frame" alt="(not connected)"></div>
  <div id="stats">0 fps</div>
  <div class="controls">
    <button id="record">Record</button>
    <label>Resolution
      <select id="resolution">
        <option value="vga">VGA</option>
        <option value="svga" selected>SVGA</option>
        <option value="uxga">UXGA</option>
      </select>
    </label>
    <label>Brightness
      <input id="brightness" type="range" min="-2" max="2" step="1" value="0">
    </label>
    <label><input id="grayscale" type="checkbox"> Grayscale</label>
  </div>
  <div id="message">Ready</div>

<script>
const portSel = document.getElementById('port');
const connectBtn = document.getElementById('connect');
const img = document.getElementById('frame');
const stats = document.getElementById('stats');
const message = document.getElementById('message');
let frameCount = 0, lastFpsTime = performance.now(), currentUrl = null;

async function refreshPorts() {
  const ports = await pywebview.api.list_ports();
  portSel.innerHTML = ports.map(p => `<option value="${p}">${p}</option>`).join('');
}

connectBtn.addEventListener('click', async () => {
  try {
    if (!portSel.value) { message.textContent = 'No serial port selected — plug in the board and try again'; return; }
    await pywebview.api.connect(portSel.value);
    connectBtn.disabled = true;
    connectBtn.textContent = 'Connected';
    message.textContent = 'Connected';
  } catch (err) {
    message.textContent = `connect failed: ${err}`;
  }
});

document.getElementById('record').addEventListener('click', async () => {
  try { message.textContent = await pywebview.api.record(); }
  catch (err) { message.textContent = String(err); }
});

document.getElementById('resolution').addEventListener('change', async (e) => {
  try { message.textContent = await pywebview.api.set_resolution(e.target.value); }
  catch (err) { message.textContent = String(err); }
});

document.getElementById('brightness').addEventListener('change', async (e) => {
  try { message.textContent = await pywebview.api.set_brightness(e.target.value); }
  catch (err) { message.textContent = String(err); }
});

document.getElementById('grayscale').addEventListener('change', async (e) => {
  try { message.textContent = await pywebview.api.set_grayscale(e.target.checked ? 'gray' : 'color'); }
  catch (err) { message.textContent = String(err); }
});

// Called from Python (reader thread) via evaluate_js for every decoded frame.
function pushFrame(base64Jpeg) {
  if (currentUrl) URL.revokeObjectURL(currentUrl);
  img.src = 'data:image/jpeg;base64,' + base64Jpeg;
  frameCount++;
  const now = performance.now();
  if (now - lastFpsTime >= 1000) {
    stats.textContent = `${frameCount} fps`;
    frameCount = 0;
    lastFpsTime = now;
  }
}

// Called from Python via evaluate_js when the reader thread detects a
// closed/errored port (cable pull, device reset). Flips the UI back to a
// disconnected state so the user can reconnect without restarting the app.
function setDisconnected() {
  connectBtn.disabled = false;
  connectBtn.textContent = 'Connect';
  message.textContent = 'Disconnected';
}

// pywebview injects window.pywebview.api asynchronously after the page
// loads; calling refreshPorts() before that fires leaves the dropdown
// empty with no retry. Wait for the ready event instead.
window.addEventListener('pywebviewready', refreshPorts);
</script>
</body>
</html>
"""


class Api:
    def __init__(self):
        self._window = None
        self._transport = UsbTransport(
            on_frame=self._push_frame, on_disconnect=self._handle_disconnect
        )

    def set_window(self, window):
        self._window = window

    def _push_frame(self, jpeg_bytes):
        import base64
        b64 = base64.b64encode(jpeg_bytes).decode("ascii")
        if self._window:
            self._window.evaluate_js(f"pushFrame('{b64}')")

    def _handle_disconnect(self):
        if self._window:
            self._window.evaluate_js("setDisconnected()")

    def list_ports(self):
        return self._transport.list_ports()

    def connect(self, port_name):
        self._transport.connect(port_name)
        return "connected"

    def record(self):
        return self._transport.send_command("RECORD")

    def set_resolution(self, value):
        return self._transport.send_command(f"RES:{value}")

    def set_brightness(self, value):
        return self._transport.send_command(f"BRIGHT:{value}")

    def set_grayscale(self, value):
        return self._transport.send_command(f"COLOR:{value}")


if __name__ == "__main__":
    import webview

    api = Api()
    window = webview.create_window("XIAO Cam — USB", html=HTML, js_api=api, width=900, height=700)
    api.set_window(window)
    webview.start()
