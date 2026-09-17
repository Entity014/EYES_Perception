from usb_gui import extract_frame, extract_line, SYNC


def test_extract_frame_waits_for_full_payload():
    buf = bytearray(SYNC + (3).to_bytes(4, "little") + b"ab")
    frame, buf = extract_frame(buf)
    assert frame is None
    assert bytes(buf) == SYNC + (3).to_bytes(4, "little") + b"ab"


def test_extract_frame_returns_complete_frame():
    payload = b"\xff\xd8\xff\xd9"
    buf = bytearray(SYNC + len(payload).to_bytes(4, "little") + payload + b"TRAILING")
    frame, buf = extract_frame(buf)
    assert frame == payload
    assert bytes(buf) == b"TRAILING"


def test_extract_frame_drops_garbage_before_sync():
    payload = b"\x01\x02"
    buf = bytearray(b"garbage" + SYNC + len(payload).to_bytes(4, "little") + payload)
    frame, buf = extract_frame(buf)
    assert frame == payload
    assert bytes(buf) == b""


def test_extract_frame_no_sync_yet():
    buf = bytearray(b"notaframe")
    frame, buf = extract_frame(buf)
    assert frame is None
    assert bytes(buf) == b"notaframe"


def test_extract_line_returns_complete_line():
    buf = bytearray(b"OK\nrest")
    line, buf = extract_line(buf)
    assert line == "OK"
    assert bytes(buf) == b"rest"


def test_extract_line_waits_for_newline():
    buf = bytearray(b"OK:recording")
    line, buf = extract_line(buf)
    assert line is None
    assert bytes(buf) == b"OK:recording"


def test_extract_line_strips_carriage_return():
    buf = bytearray(b"ERR:bad\r\n")
    line, buf = extract_line(buf)
    assert line == "ERR:bad"
