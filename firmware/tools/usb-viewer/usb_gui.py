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


def extract_frame(buf):
    i = buf.find(SYNC)
    if i == -1:
        # No sync yet; keep the buffer unchanged for text or next frame data
        return None, buf
    if i > 0:
        del buf[:i]
    if len(buf) < 6:
        return None, buf
    length = int.from_bytes(buf[2:6], "little")
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


class UsbTransport:
    def __init__(self, on_frame, on_status=None):
        self._on_frame = on_frame
        self._on_status = on_status
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
        return self._port is not None and self._port.is_open

    def send_command(self, line, timeout=2.0):
        if not self.is_connected():
            raise RuntimeError("not connected")
        with self._reply_q.mutex:
            self._reply_q.queue.clear()
        self._port.write((line + "\n").encode("ascii"))
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
                break
            if chunk:
                self._buf.extend(chunk)
            while True:
                frame, self._buf = extract_frame(self._buf)
                if frame is None:
                    break
                self._on_frame(frame)
            while True:
                line, self._buf = extract_line(self._buf)
                if line is None:
                    break
                self._reply_q.put(line)
                if self._on_status and line.startswith("OK:"):
                    self._on_status(line)
