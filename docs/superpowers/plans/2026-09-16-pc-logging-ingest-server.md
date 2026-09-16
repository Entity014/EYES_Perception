# PC Logging Pipeline — Ingest Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Node.js server that accepts the ESP32's frame stream over TCP, stores it to disk + SQLite when a recording session is open, relays live frames to browsers over WebSocket, and exposes the REST API the frontend (separate plan) needs for sessions, playback, export, and camera control.

**Architecture:** `server/src/protocol.js` is a pure, dependency-free streaming parser for the frame format `pcstream` (firmware plan) sends. `server/src/ingest.js` is a raw TCP server built on that parser: every frame is relayed to connected WebSocket viewers, and — only while a session is open — persisted as a file plus a SQLite row. `server/src/db.js` wraps `better-sqlite3`. `server/src/api.js` is a plain Node `http` server (no framework — this app is small enough that Express would just be another dependency to track) exposing REST endpoints, mounted alongside the WebSocket server from `server/src/index.js`.

**Tech Stack:** Node.js (built-in `http`, `net`, `node:test`), `better-sqlite3`, `ws`, `archiver` (zip export).

**Spec:** [docs/superpowers/specs/2026-09-16-pc-logging-pipeline-design.md](../specs/2026-09-16-pc-logging-pipeline-design.md)

## Global Constraints

- Every frame carries `{flag (live=0/backlog=1), sequence, payload}` — see the firmware plan's Task 4 `sendFramed()` for the exact wire format this must parse.
- A session with no gaps in its stored sequence range is how "no frame loss" gets verified — `db.js` must expose a way to compute gaps, not just store rows.
- Live viewing works with no session open (frames relay over WebSocket either way); persistence to disk/DB only happens while a session is open — this mirrors the original firmware's Record button semantics (view without recording is allowed).
- No framework dependency beyond what's listed above — keep this small and inspectable.

---

## File Structure

- Create: `server/package.json`
- Create: `server/src/protocol.js` — streaming frame parser (pure logic, no I/O)
- Create: `server/test/protocol.test.js`
- Create: `server/src/db.js` — SQLite schema + query functions
- Create: `server/test/db.test.js`
- Create: `server/src/ingest.js` — TCP listener wiring protocol + db + file storage + WS relay
- Create: `server/src/ws.js` — WebSocket live-relay hub
- Create: `server/src/api.js` — REST endpoints
- Create: `server/src/index.js` — process entry point, wires everything together
- Create: `server/test/manual-fake-esp32.js` — throwaway script for the end-to-end manual test in Task 6

---

## Task 1: Project scaffold

**Files:**
- Create: `server/package.json`

- [ ] **Step 1: Create the directory and package.json**

```bash
mkdir -p "/home/xero/Master's Degree/eyes_perception/server/src" "/home/xero/Master's Degree/eyes_perception/server/test" "/home/xero/Master's Degree/eyes_perception/server/data"
```

Create `server/package.json`:

```json
{
  "name": "eyes-perception-server",
  "version": "0.1.0",
  "private": true,
  "type": "module",
  "scripts": {
    "start": "node src/index.js",
    "test": "node --test test/"
  },
  "dependencies": {
    "better-sqlite3": "^11.3.0",
    "ws": "^8.18.0",
    "archiver": "^7.0.1"
  }
}
```

- [ ] **Step 2: Install dependencies**

Run: `cd server && npm install`
Expected: installs without error, creates `node_modules/` and `package-lock.json`

- [ ] **Step 3: Commit**

```bash
git add server/package.json server/package-lock.json
git commit -m "chore(server): scaffold Node ingest server project"
```

(`node_modules/` must already be excluded by a `.gitignore` — if `server/.gitignore` doesn't exist yet, create it first with a single line `node_modules/` and add it to this commit.)

---

## Task 2: Frame protocol parser

**Files:**
- Create: `server/src/protocol.js`
- Test: `server/test/protocol.test.js`

**Interfaces:**
- Produces: `class FrameParser { push(chunk: Buffer): Array<{flag: number, sequence: number, payload: Buffer}> }`. Consumed by `ingest.js` (Task 4), one `FrameParser` instance per TCP connection.

Wire format (must match `pcstream.cpp`'s `sendFramed()` in the firmware plan exactly): `0xAA 0x55` sync + 1-byte flag + 4-byte little-endian sequence + 4-byte little-endian length + payload bytes.

- [ ] **Step 1: Write the failing test**

Create `server/test/protocol.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { FrameParser } from '../src/protocol.js';

function encodeFrame(flag, seq, payload) {
  const header = Buffer.alloc(11);
  header[0] = 0xAA; header[1] = 0x55; header[2] = flag;
  header.writeUInt32LE(seq, 3);
  header.writeUInt32LE(payload.length, 7);
  return Buffer.concat([header, payload]);
}

test('parses a single complete frame in one push', () => {
  const parser = new FrameParser();
  const payload = Buffer.from('hello-jpeg-bytes');
  const frames = parser.push(encodeFrame(0, 42, payload));
  assert.equal(frames.length, 1);
  assert.equal(frames[0].flag, 0);
  assert.equal(frames[0].sequence, 42);
  assert.deepEqual(frames[0].payload, payload);
});

test('parses a frame split across multiple pushes', () => {
  const parser = new FrameParser();
  const payload = Buffer.from('split-payload');
  const full = encodeFrame(1, 7, payload);
  const first = full.subarray(0, 5);
  const second = full.subarray(5);
  assert.equal(parser.push(first).length, 0);
  const frames = parser.push(second);
  assert.equal(frames.length, 1);
  assert.equal(frames[0].flag, 1);
  assert.equal(frames[0].sequence, 7);
  assert.deepEqual(frames[0].payload, payload);
});

test('parses multiple frames delivered in one push', () => {
  const parser = new FrameParser();
  const a = encodeFrame(0, 1, Buffer.from('aaa'));
  const b = encodeFrame(0, 2, Buffer.from('bb'));
  const frames = parser.push(Buffer.concat([a, b]));
  assert.equal(frames.length, 2);
  assert.equal(frames[0].sequence, 1);
  assert.equal(frames[1].sequence, 2);
});

test('resyncs past stray bytes before the sync marker', () => {
  const parser = new FrameParser();
  const junk = Buffer.from([0x00, 0x01, 0x02]);
  const good = encodeFrame(0, 5, Buffer.from('ok'));
  const frames = parser.push(Buffer.concat([junk, good]));
  assert.equal(frames.length, 1);
  assert.equal(frames[0].sequence, 5);
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd server && npm test`
Expected: FAIL — `src/protocol.js` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Create `server/src/protocol.js`:

```js
const SYNC0 = 0xAA;
const SYNC1 = 0x55;
const HEADER_LEN = 11; // sync(2) + flag(1) + seq(4) + len(4)

// Streaming parser for pcstream's wire format. Feed it arbitrarily-chunked
// TCP data via push(); it returns any complete frames that chunk finished.
export class FrameParser {
  constructor() {
    this._buf = Buffer.alloc(0);
  }

  push(chunk) {
    this._buf = this._buf.length ? Buffer.concat([this._buf, chunk]) : chunk;
    const frames = [];
    for (;;) {
      const syncIndex = this._findSync();
      if (syncIndex === -1) {
        // No sync found at all: keep at most the last byte (it might be
        // the first half of a sync marker split across pushes).
        if (this._buf.length > 1) this._buf = this._buf.subarray(this._buf.length - 1);
        break;
      }
      if (syncIndex > 0) this._buf = this._buf.subarray(syncIndex);
      if (this._buf.length < HEADER_LEN) break;

      const flag = this._buf[2];
      const sequence = this._buf.readUInt32LE(3);
      const len = this._buf.readUInt32LE(7);
      const total = HEADER_LEN + len;
      if (this._buf.length < total) break;

      frames.push({ flag, sequence, payload: Buffer.from(this._buf.subarray(HEADER_LEN, total)) });
      this._buf = this._buf.subarray(total);
    }
    return frames;
  }

  _findSync() {
    for (let i = 0; i + 1 < this._buf.length; i++) {
      if (this._buf[i] === SYNC0 && this._buf[i + 1] === SYNC1) return i;
    }
    return -1;
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd server && npm test`
Expected: PASS (4 tests)

- [ ] **Step 5: Commit**

```bash
git add server/src/protocol.js server/test/protocol.test.js
git commit -m "feat(server): add pcstream frame protocol parser"
```

---

## Task 3: SQLite schema and db module

**Files:**
- Create: `server/src/db.js`
- Test: `server/test/db.test.js`

**Interfaces:**
- Produces: `function openDb(path)` returning an object with `createSession()`, `endSession(id)`, `getOpenSession()`, `insertFrame({sessionId, sequence, timestamp, source, path})`, `getSessions()`, `getFramesForSession(sessionId)`, `getSequenceGaps(sessionId)`. Consumed by `ingest.js` (Task 4) and `api.js` (Task 6).

- [ ] **Step 1: Write the failing test**

Create `server/test/db.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { openDb } from '../src/db.js';

test('session lifecycle and frame storage', () => {
  const db = openDb(':memory:');
  assert.equal(db.getOpenSession(), null);

  const session = db.createSession();
  assert.equal(db.getOpenSession().id, session.id);

  db.insertFrame({ sessionId: session.id, sequence: 0, timestamp: 1000, source: 'live', path: 'frame_0.jpg' });
  db.insertFrame({ sessionId: session.id, sequence: 1, timestamp: 1040, source: 'live', path: 'frame_1.jpg' });
  db.insertFrame({ sessionId: session.id, sequence: 3, timestamp: 1120, source: 'backlog', path: 'frame_3.jpg' });

  const frames = db.getFramesForSession(session.id);
  assert.equal(frames.length, 3);
  assert.equal(frames[0].sequence, 0);

  const gaps = db.getSequenceGaps(session.id);
  assert.deepEqual(gaps, [2]);

  db.endSession(session.id);
  assert.equal(db.getOpenSession(), null);

  const sessions = db.getSessions();
  assert.equal(sessions.length, 1);
  assert.ok(sessions[0].ended_at !== null);
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd server && npm test`
Expected: FAIL — `src/db.js` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Create `server/src/db.js`:

```js
import Database from 'better-sqlite3';

export function openDb(path) {
  const db = new Database(path);
  db.pragma('journal_mode = WAL');
  db.exec(`
    CREATE TABLE IF NOT EXISTS sessions (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      started_at INTEGER NOT NULL,
      ended_at INTEGER
    );
    CREATE TABLE IF NOT EXISTS frames (
      session_id INTEGER NOT NULL,
      sequence INTEGER NOT NULL,
      timestamp INTEGER NOT NULL,
      source TEXT NOT NULL,
      path TEXT NOT NULL,
      PRIMARY KEY (session_id, sequence)
    );
  `);

  return {
    createSession() {
      const startedAt = Date.now();
      const { lastInsertRowid } = db.prepare(
        'INSERT INTO sessions (started_at, ended_at) VALUES (?, NULL)'
      ).run(startedAt);
      return { id: Number(lastInsertRowid), started_at: startedAt };
    },

    endSession(id) {
      db.prepare('UPDATE sessions SET ended_at = ? WHERE id = ?').run(Date.now(), id);
    },

    getOpenSession() {
      const row = db.prepare('SELECT * FROM sessions WHERE ended_at IS NULL ORDER BY id DESC LIMIT 1').get();
      return row ?? null;
    },

    insertFrame({ sessionId, sequence, timestamp, source, path }) {
      db.prepare(
        'INSERT OR IGNORE INTO frames (session_id, sequence, timestamp, source, path) VALUES (?, ?, ?, ?, ?)'
      ).run(sessionId, sequence, timestamp, source, path);
    },

    getSessions() {
      return db.prepare('SELECT * FROM sessions ORDER BY id DESC').all();
    },

    getFramesForSession(sessionId) {
      return db.prepare('SELECT * FROM frames WHERE session_id = ? ORDER BY sequence ASC').all(sessionId);
    },

    getSequenceGaps(sessionId) {
      const rows = db.prepare('SELECT sequence FROM frames WHERE session_id = ? ORDER BY sequence ASC').all(sessionId);
      const gaps = [];
      for (let i = 1; i < rows.length; i++) {
        for (let seq = rows[i - 1].sequence + 1; seq < rows[i].sequence; seq++) gaps.push(seq);
      }
      return gaps;
    },
  };
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd server && npm test`
Expected: PASS (all tests, including protocol.test.js from Task 2)

- [ ] **Step 5: Commit**

```bash
git add server/src/db.js server/test/db.test.js
git commit -m "feat(server): add SQLite session/frame storage"
```

---

## Task 4: TCP ingest server

**Files:**
- Create: `server/src/ingest.js`

**Interfaces:**
- Consumes: `FrameParser` (Task 2), `db` object from `openDb()` (Task 3), and a `broadcastLive(frameBuffer)` callback (Task 5) for relaying to WebSocket viewers.
- Produces: `function startIngestServer({ port, db, dataDir, broadcastLive })` returning the `net.Server` instance. Called from `index.js` (Task 7).

- [ ] **Step 1: Implement it**

Create `server/src/ingest.js`:

```js
import net from 'node:net';
import fs from 'node:fs';
import path from 'node:path';
import { FrameParser } from './protocol.js';

export function startIngestServer({ port, db, dataDir, broadcastLive }) {
  const server = net.createServer((socket) => {
    const parser = new FrameParser();

    socket.on('data', (chunk) => {
      for (const frame of parser.push(chunk)) {
        broadcastLive(frame.payload);

        const session = db.getOpenSession();
        if (!session) continue; // viewing without recording — don't persist

        const filename = `frame_${frame.sequence}.jpg`;
        const sessionDir = path.join(dataDir, String(session.id));
        fs.mkdirSync(sessionDir, { recursive: true });
        fs.writeFileSync(path.join(sessionDir, filename), frame.payload);

        db.insertFrame({
          sessionId: session.id,
          sequence: frame.sequence,
          timestamp: Date.now(),
          source: frame.flag === 1 ? 'backlog' : 'live',
          path: filename,
        });
      }
    });

    socket.on('error', () => {}); // a dropped ESP32 connection is expected, not fatal
  });

  server.listen(port);
  return server;
}
```

- [ ] **Step 2: Manual smoke test**

This will be exercised end-to-end in Task 6's manual test script once `index.js` exists — no isolated test here since it's pure wiring over Tasks 2-3, both already unit tested.

- [ ] **Step 3: Commit**

```bash
git add server/src/ingest.js
git commit -m "feat(server): add TCP ingest server"
```

---

## Task 5: WebSocket live relay

**Files:**
- Create: `server/src/ws.js`

**Interfaces:**
- Produces: `function createLiveHub(httpServer)` returning `{ broadcast(payloadBuffer) }`. The `broadcast` function is passed as `broadcastLive` to `startIngestServer()` (Task 4). Consumed by `index.js` (Task 7).

- [ ] **Step 1: Implement it**

Create `server/src/ws.js`:

```js
import { WebSocketServer } from 'ws';

export function createLiveHub(httpServer) {
  const wss = new WebSocketServer({ server: httpServer, path: '/live' });

  return {
    broadcast(payload) {
      for (const client of wss.clients) {
        if (client.readyState === client.OPEN) client.send(payload);
      }
    },
  };
}
```

- [ ] **Step 2: Commit**

```bash
git add server/src/ws.js
git commit -m "feat(server): add WebSocket live frame relay"
```

---

## Task 6: REST API

**Files:**
- Create: `server/src/api.js`

**Interfaces:**
- Consumes: `db` (Task 3), and an `esp32Host`/`esp32Port` config for forwarding `/resolution` and `/colormode`.
- Produces: `function createApiServer({ db, dataDir, esp32Host, esp32Port })` returning a `node:http` server. Consumed by `index.js` (Task 7), and by the frontend plan as the base URL for all its `fetch()` calls.

- [ ] **Step 1: Implement it**

Create `server/src/api.js`:

```js
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import archiver from 'archiver';

function sendJson(res, status, body) {
  res.writeHead(status, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify(body));
}

async function readBody(req) {
  const chunks = [];
  for await (const chunk of req) chunks.push(chunk);
  return Buffer.concat(chunks).toString('utf8');
}

async function forwardToEsp32(host, port, urlPath, body) {
  const res = await fetch(`http://${host}:${port}${urlPath}`, { method: 'POST', body });
  return res.ok;
}

export function createApiServer({ db, dataDir, esp32Host, esp32Port }) {
  return http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url, 'http://localhost');

      if (req.method === 'POST' && url.pathname === '/session/start') {
        return sendJson(res, 200, db.createSession());
      }

      if (req.method === 'POST' && url.pathname === '/session/stop') {
        const open = db.getOpenSession();
        if (!open) return sendJson(res, 400, { error: 'no open session' });
        db.endSession(open.id);
        return sendJson(res, 200, { id: open.id });
      }

      if (req.method === 'GET' && url.pathname === '/sessions') {
        return sendJson(res, 200, db.getSessions());
      }

      const frameListMatch = url.pathname.match(/^\/session\/(\d+)\/frames$/);
      if (req.method === 'GET' && frameListMatch) {
        return sendJson(res, 200, db.getFramesForSession(Number(frameListMatch[1])));
      }

      const frameFileMatch = url.pathname.match(/^\/session\/(\d+)\/frame\/(\d+)\.jpg$/);
      if (req.method === 'GET' && frameFileMatch) {
        const [, sessionId, sequence] = frameFileMatch;
        const filePath = path.join(dataDir, sessionId, `frame_${sequence}.jpg`);
        if (!fs.existsSync(filePath)) return sendJson(res, 404, { error: 'not found' });
        res.writeHead(200, { 'Content-Type': 'image/jpeg' });
        return fs.createReadStream(filePath).pipe(res);
      }

      const exportMatch = url.pathname.match(/^\/session\/(\d+)\/export$/);
      if (req.method === 'GET' && exportMatch) {
        const sessionId = Number(exportMatch[1]);
        const frames = db.getFramesForSession(sessionId);
        res.writeHead(200, {
          'Content-Type': 'application/zip',
          'Content-Disposition': `attachment; filename="session_${sessionId}.zip"`,
        });
        const archive = archiver('zip');
        archive.pipe(res);
        const csvLines = ['sequence,timestamp,source'];
        for (const frame of frames) {
          const filePath = path.join(dataDir, String(sessionId), frame.path);
          archive.file(filePath, { name: frame.path });
          csvLines.push(`${frame.sequence},${frame.timestamp},${frame.source}`);
        }
        archive.append(csvLines.join('\n'), { name: 'metadata.csv' });
        return archive.finalize();
      }

      if (req.method === 'POST' && url.pathname === '/resolution') {
        const ok = await forwardToEsp32(esp32Host, esp32Port, '/resolution', await readBody(req));
        return sendJson(res, ok ? 200 : 502, { ok });
      }

      if (req.method === 'POST' && url.pathname === '/colormode') {
        const ok = await forwardToEsp32(esp32Host, esp32Port, '/colormode', await readBody(req));
        return sendJson(res, ok ? 200 : 502, { ok });
      }

      sendJson(res, 404, { error: 'not found' });
    } catch (err) {
      sendJson(res, 500, { error: String(err) });
    }
  });
}
```

- [ ] **Step 2: Commit**

```bash
git add server/src/api.js
git commit -m "feat(server): add REST API for sessions, export, and camera control"
```

---

## Task 7: Wire it together and run an end-to-end manual test

**Files:**
- Create: `server/src/index.js`
- Create: `server/test/manual-fake-esp32.js`

**Interfaces:**
- Consumes: `openDb` (Task 3), `startIngestServer` (Task 4), `createLiveHub` (Task 5), `createApiServer` (Task 6).

- [ ] **Step 1: Implement the entry point**

Create `server/src/index.js`:

```js
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { openDb } from './db.js';
import { startIngestServer } from './ingest.js';
import { createLiveHub } from './ws.js';
import { createApiServer } from './api.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const dataDir = path.join(__dirname, '..', 'data');
const dbPath = path.join(dataDir, 'log.sqlite');

const db = openDb(dbPath);
const apiServer = createApiServer({
  db,
  dataDir,
  esp32Host: process.env.ESP32_HOST ?? 'xiao-cam.local',
  esp32Port: Number(process.env.ESP32_PORT ?? 80),
});
const liveHub = createLiveHub(apiServer);
startIngestServer({
  port: Number(process.env.INGEST_PORT ?? 9000),
  db,
  dataDir,
  broadcastLive: liveHub.broadcast,
});

const apiPort = Number(process.env.API_PORT ?? 8080);
apiServer.listen(apiPort, () => {
  console.log(`API + live WS on :${apiPort}, TCP ingest on :${process.env.INGEST_PORT ?? 9000}`);
});
```

- [ ] **Step 2: Write the manual fake-ESP32 test script**

Create `server/test/manual-fake-esp32.js` (not part of the automated `npm test` suite — a throwaway script for verifying the full pipeline without real hardware, per the spec's testing section):

```js
// Usage: node test/manual-fake-esp32.js
// Connects to the running ingest server and sends 20 synthetic frames,
// verifying (by calling the REST API) that they all landed with no gaps.
import net from 'node:net';

const INGEST_PORT = Number(process.env.INGEST_PORT ?? 9000);
const API_PORT = Number(process.env.API_PORT ?? 8080);

function encodeFrame(flag, seq, payload) {
  const header = Buffer.alloc(11);
  header[0] = 0xAA; header[1] = 0x55; header[2] = flag;
  header.writeUInt32LE(seq, 3);
  header.writeUInt32LE(payload.length, 7);
  return Buffer.concat([header, payload]);
}

const startRes = await fetch(`http://localhost:${API_PORT}/session/start`, { method: 'POST' });
const { id: sessionId } = await startRes.json();
console.log('opened session', sessionId);

const socket = net.connect(INGEST_PORT, 'localhost', async () => {
  for (let seq = 0; seq < 20; seq++) {
    socket.write(encodeFrame(0, seq, Buffer.from(`fake-jpeg-${seq}`)));
  }
  socket.end();

  setTimeout(async () => {
    await fetch(`http://localhost:${API_PORT}/session/stop`, { method: 'POST' });
    const frames = await (await fetch(`http://localhost:${API_PORT}/session/${sessionId}/frames`)).json();
    console.log(`stored ${frames.length}/20 frames`);
    if (frames.length !== 20) {
      console.error('FAIL: expected 20 frames');
      process.exit(1);
    }
    console.log('PASS: all frames stored with no gaps');
  }, 500);
});
```

- [ ] **Step 3: Run the manual end-to-end test**

In one terminal: `cd server && npm start`
In another: `cd server && node test/manual-fake-esp32.js`
Expected output: `PASS: all frames stored with no gaps`

- [ ] **Step 4: Commit**

```bash
git add server/src/index.js server/test/manual-fake-esp32.js
git commit -m "feat(server): wire ingest/API/WS together; add manual e2e test script"
```

---

## Self-Review Notes

- **Spec coverage:** frame parsing ✓, session-gated persistence ✓, sequence-gap verification ✓ (`getSequenceGaps`, exercised directly in `db.test.js` and indirectly in the manual e2e script), live WS relay ✓, export as zip+CSV ✓, resolution/colormode forwarding to the ESP32 ✓.
- **Type consistency:** `FrameParser.push()`'s returned `{flag, sequence, payload}` shape (Task 2) matches what `ingest.js` (Task 4) destructures. `db.insertFrame()`'s parameter names match between its definition (Task 3) and its only caller (Task 4).
- **No placeholders:** every step has runnable code; the manual test script is explicitly labeled as such per the spec (which defers PC-side automated testing to the implementation plan — this plan interprets that as "cover the pure logic with `node:test`, cover the wiring with one real manual run").
