const SYNC0 = 0xaa;
const SYNC1 = 0x55;
const HEADER_LEN = 11;
const MAX_PAYLOAD_LEN = 200000;

export class FrameParser {
  constructor() {
    this._buf = Buffer.alloc(0);
  }

  push(chunk) {
    if (!Buffer.isBuffer(chunk)) chunk = Buffer.from(chunk);
    this._buf = this._buf.length ? Buffer.concat([this._buf, chunk]) : chunk;
    const frames = [];

    for (;;) {
      const syncIndex = this._findSync();
      if (syncIndex === -1) {
        this._buf = this._buf.length > 1 ? this._buf.subarray(-1) : this._buf;
        break;
      }
      if (syncIndex > 0) this._buf = this._buf.subarray(syncIndex);
      if (this._buf.length < HEADER_LEN) break;

      const flag = this._buf[2];
      const sequence = this._buf.readUInt32LE(3);
      const length = this._buf.readUInt32LE(7);
      if (length > MAX_PAYLOAD_LEN) {
        this._buf = this._buf.subarray(2);
        continue;
      }

      const total = HEADER_LEN + length;
      if (this._buf.length < total) break;
      frames.push({
        flag,
        sequence,
        payload: Buffer.from(this._buf.subarray(HEADER_LEN, total)),
      });
      this._buf = this._buf.subarray(total);
    }
    return frames;
  }

  _findSync() {
    for (let index = 0; index + 1 < this._buf.length; index++) {
      if (this._buf[index] === SYNC0 && this._buf[index + 1] === SYNC1) return index;
    }
    return -1;
  }
}