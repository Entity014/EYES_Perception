import { test } from 'node:test';
import assert from 'node:assert/strict';
import { openDb } from '../src/db.js';

test('stores session frames and reports sequence gaps', () => {
  const db = openDb(':memory:');
  assert.equal(db.getOpenSession(), null);
  const session = db.createSession();
  db.insertFrame({ sessionId: session.id, sequence: 0, timestamp: 1000, source: 'live', path: 'frame_0.jpg' });
  db.insertFrame({ sessionId: session.id, sequence: 1, timestamp: 1040, source: 'live', path: 'frame_1.jpg' });
  db.insertFrame({ sessionId: session.id, sequence: 3, timestamp: 1120, source: 'backlog', path: 'frame_3.jpg' });
  assert.equal(db.getFramesForSession(session.id).length, 3);
  assert.deepEqual(db.getSequenceGaps(session.id), [2]);
  db.endSession(session.id);
  assert.equal(db.getOpenSession(), null);
  assert.ok(db.getSessions()[0].ended_at !== null);
  db.close();
});