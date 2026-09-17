import { test } from 'node:test';
import assert from 'node:assert/strict';
import { computeHoldMs } from '../lib/playback.js';

test('uses the real timestamp gap', () => {
  assert.equal(computeHoldMs([{ timestamp: 1000 }, { timestamp: 1080 }], 0), 80);
});

test('clamps long, zero, and final-frame gaps', () => {
  const frames = [{ timestamp: 1000 }, { timestamp: 1000 }, { timestamp: 60000 }];
  assert.equal(computeHoldMs(frames, 1, { minMs: 16, maxMs: 500 }), 500);
  assert.equal(computeHoldMs(frames, 0, { minMs: 16, maxMs: 500 }), 16);
  assert.equal(computeHoldMs(frames, 2, { minMs: 16, maxMs: 500 }), 500);
});