from usb_gui import (
    FILE_SYNC,
    MAX_FILE_CHUNK_LEN,
    MAX_FRAME_LEN,
    SYNC,
    demux_step,
    extract_file_chunk,
    extract_frame,
    extract_line,
)


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


def test_extract_file_chunk_returns_data_chunk():
    payload = b"RIFF...AVI DATA"
    buf = bytearray(FILE_SYNC + len(payload).to_bytes(4, "little") + payload + b"TRAILING")
    chunk, buf = extract_file_chunk(buf)
    assert chunk == payload
    assert bytes(buf) == b"TRAILING"


def test_extract_file_chunk_zero_length_is_eof_marker_not_rejected():
    # Unlike extract_frame, a zero-length chunk here is meaningful (EOF),
    # not a bad/rejected length.
    buf = bytearray(FILE_SYNC + (0).to_bytes(4, "little") + b"TRAILING")
    chunk, buf = extract_file_chunk(buf)
    assert chunk == b""
    assert bytes(buf) == b"TRAILING"


def test_extract_file_chunk_waits_for_full_payload():
    buf = bytearray(FILE_SYNC + (5).to_bytes(4, "little") + b"ab")
    chunk, buf = extract_file_chunk(buf)
    assert chunk is None
    assert bytes(buf) == FILE_SYNC + (5).to_bytes(4, "little") + b"ab"


def test_extract_file_chunk_rejects_over_max_length_and_resyncs():
    length_bytes = (MAX_FILE_CHUNK_LEN + 1).to_bytes(4, "little")
    payload = b"\x01\x02"
    buf = bytearray(FILE_SYNC + length_bytes + FILE_SYNC + len(payload).to_bytes(4, "little") + payload)
    chunk, buf = extract_file_chunk(buf)
    assert chunk == payload
    assert bytes(buf) == b""


def test_demux_step_distinguishes_file_chunk_from_frame_and_line():
    payload = b"\xff\xd8\xff\xd9"
    file_payload = b"AVI DATA"
    buf = bytearray(
        b"OK\n"
        + SYNC + len(payload).to_bytes(4, "little") + payload
        + FILE_SYNC + len(file_payload).to_bytes(4, "little") + file_payload
        + FILE_SYNC + (0).to_bytes(4, "little")  # EOF marker
    )

    kind, value, buf = demux_step(buf)
    assert (kind, value) == ("line", "OK")

    kind, value, buf = demux_step(buf)
    assert (kind, value) == ("frame", payload)

    kind, value, buf = demux_step(buf)
    assert (kind, value) == ("file_chunk", file_payload)

    kind, value, buf = demux_step(buf)
    assert (kind, value) == ("file_chunk", b"")

    kind, value, buf = demux_step(buf)
    assert kind is None
    assert bytes(buf) == b""


def test_demux_step_discards_garbage_line_that_is_not_a_real_reply():
    # Leading noise containing a coincidental 0x0A before the next real
    # marker must not be surfaced as a fake "reply" -- it should be
    # silently discarded, and the real frame after it recovered normally.
    payload = b"\xff\xd8\xff\xd9"
    buf = bytearray(b"\x91\x0a\x02garbage" + SYNC + len(payload).to_bytes(4, "little") + payload)

    kind, value, buf = demux_step(buf)
    assert (kind, value) == ("frame", payload)
    assert bytes(buf) == b""


def test_demux_step_still_accepts_real_ok_and_err_replies():
    buf = bytearray(b"OK\nERR:unknown command\n")

    kind, value, buf = demux_step(buf)
    assert (kind, value) == ("line", "OK")

    kind, value, buf = demux_step(buf)
    assert (kind, value) == ("line", "ERR:unknown command")
