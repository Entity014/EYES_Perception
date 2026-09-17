import { test } from 'node:test';
import assert from 'node:assert/strict';
import { shouldBroadcastLive, shouldStoreInSession } from '../src/ingest.js';
import { FrameParser } from '../src/protocol.js';

function encodeFrame(flag, sequence, payload) {
  const header = Buffer.alloc(11);
  header[0] = 0xaa; header[1] = 0x55; header[2] = flag;
  header.writeUInt32LE(sequence, 3);
  header.writeUInt32LE(payload.length, 7);
  return Buffer.concat([header, payload]);
}

test('parses complete, split, multiple, and resynchronized frames', () => {
  const parser = new FrameParser();
  const first = encodeFrame(0, 42, Buffer.from('hello'));
  const second = encodeFrame(1, 7, Buffer.from('split'));
  assert.deepEqual(parser.push(first), [{ flag: 0, sequence: 42, payload: Buffer.from('hello') }]);
  assert.deepEqual(parser.push(Buffer.concat([Buffer.from([0, 1]), second.subarray(0, 5)])), []);
  assert.deepEqual(parser.push(second.subarray(5)), [{ flag: 1, sequence: 7, payload: Buffer.from('split') }]);
  assert.deepEqual(parser.push(Buffer.concat([encodeFrame(0, 1, Buffer.from('a')), encodeFrame(0, 2, Buffer.from('b'))])).map((frame) => frame.sequence), [1, 2]);
});

test('does not broadcast backlog frames to the live view', () => {
  assert.equal(shouldBroadcastLive({ flag: 0 }), true);
  assert.equal(shouldBroadcastLive({ flag: 1 }), false);
});

test('stores deferred backlog frames, not live preview frames', () => {
  assert.equal(shouldStoreInSession({ flag: 0 }), false);
  assert.equal(shouldStoreInSession({ flag: 1 }), true);
});
