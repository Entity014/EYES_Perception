from usb_gui import demux_step, extract_frame, extract_line, MAX_FRAME_LEN, SYNC


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


def test_extract_frame_rejects_bogus_huge_length_and_resyncs():
    # A "length" this large would never arrive; extract_frame must not wait
    # for it forever. It should drop the false sync and resync instead of
    # returning with the buffer untouched (which would just re-find the
    # same bad sync on the next call and spin forever).
    bogus = SYNC + (0xFFFFFFFF).to_bytes(4, "little")
    payload = b"\x01\x02"
    buf = bytearray(bogus + SYNC + len(payload).to_bytes(4, "little") + payload)
    frame, buf = extract_frame(buf)
    assert frame == payload
    assert bytes(buf) == b""


def test_extract_frame_rejects_length_over_max():
    length_bytes = (MAX_FRAME_LEN + 1).to_bytes(4, "little")
    buf = bytearray(SYNC + length_bytes + b"junk")
    frame, buf = extract_frame(buf)
    assert frame is None
    # The bad sync (2 bytes) was dropped so the next call can resync; no
    # real sync marker follows here so we're left waiting, not spinning.
    assert bytes(buf) == length_bytes + b"junk"


def test_extract_frame_rejects_zero_length():
    buf = bytearray(SYNC + (0).to_bytes(4, "little") + b"trailing")
    frame, buf = extract_frame(buf)
    assert frame is None
    assert bytes(buf) == (0).to_bytes(4, "little") + b"trailing"


def test_demux_step_recovers_reply_before_frame_in_same_buffer():
    # Regression for the frame-first demux bug: a complete "OK\n" reply
    # sitting in the buffer immediately before a frame's sync bytes must
    # not be destroyed by extract_frame()'s "delete everything before the
    # sync" behavior. Demuxing must process the stream in actual order.
    payload = b"\xff\xd8\xff\xd9"
    buf = bytearray(b"OK\n" + SYNC + len(payload).to_bytes(4, "little") + payload)

    kind, value, buf = demux_step(buf)
    assert (kind, value) == ("line", "OK")

    kind, value, buf = demux_step(buf)
    assert (kind, value) == ("frame", payload)

    kind, value, buf = demux_step(buf)
    assert kind is None
    assert bytes(buf) == b""


def test_demux_step_does_not_mistake_payload_newline_for_line_terminator():
    # A 0x0A byte inside the JPEG payload must not be treated as a line
    # terminator just because it appears before the frame is complete.
    payload = b"\xff\xd8\x0a\xff\xd9"
    buf = bytearray(SYNC + len(payload).to_bytes(4, "little") + payload)

    kind, value, buf = demux_step(buf)
    assert (kind, value) == ("frame", payload)
    assert bytes(buf) == b""
