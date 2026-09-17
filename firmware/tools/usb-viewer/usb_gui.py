"""Desktop USB client for the XIAO cam: live view + camera controls,
talking to the firmware over USB CDC serial instead of WiFi. Single file
by design — see docs/superpowers/specs/2026-09-17-usb-gui-prototype-design.md.
"""

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
