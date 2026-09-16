# PC Logging Pipeline — Frontend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Next.js/React frontend: a live-view page (live frame feed, Record button, resolution/grayscale controls) and a log-browser page (list recorded sessions, preview-play a session's frames, download a session as zip+CSV).

**Architecture:** A single Next.js App Router project with two pages. `lib/api.js` is a thin wrapper around the ingest server's REST API (separate plan) — every network call goes through it, nothing else in the app calls `fetch` directly. `lib/playback.js` is the one piece of real logic in this plan (how long to hold each frame during preview playback) and is unit tested in isolation; everything else is verified by running the dev server and using it in a browser, per this project's UI-testing convention.

**Tech Stack:** Next.js (App Router), React, native `WebSocket` and `fetch` — no state-management library, no UI kit (the surface area doesn't need one).

**Spec:** [docs/superpowers/specs/2026-09-16-pc-logging-pipeline-design.md](../specs/2026-09-16-pc-logging-pipeline-design.md)

## Global Constraints

- The frontend never talks to the ESP32 directly — every request goes to the PC ingest/API server (`server/` plan), which forwards `/resolution` and `/colormode` to the ESP32 itself.
- Live frames arrive over the WebSocket `/live` endpoint as raw binary JPEG messages (see the ingest-server plan's `ws.js`) — one message per frame, no envelope/JSON wrapping.
- Session frame metadata (`GET /session/:id/frames`) returns rows shaped `{session_id, sequence, timestamp, source, path}` — see the ingest-server plan's `db.js`.
- All API base URLs come from one env var (`NEXT_PUBLIC_API_BASE`), never hardcoded per-component.

---

## File Structure

- Create: `frontend/` (scaffolded via `create-next-app`)
- Create: `frontend/lib/api.js` — REST/WS URL helpers, one function per endpoint
- Create: `frontend/lib/playback.js` — pure frame-hold-time calculation
- Create: `frontend/test/playback.test.js`
- Create: `frontend/components/LiveView.jsx`
- Create: `frontend/components/ControlsPanel.jsx`
- Create: `frontend/app/page.jsx` — live view page
- Create: `frontend/components/SessionList.jsx`
- Create: `frontend/components/FramePreviewPlayer.jsx`
- Create: `frontend/app/log/page.jsx` — log browser page
- Create: `frontend/.env.local`

---

## Task 1: Scaffold the Next.js project

**Files:**
- Create: `frontend/` (generated)

- [ ] **Step 1: Run create-next-app non-interactively**

```bash
cd "/home/xero/Master's Degree/eyes_perception"
npx create-next-app@latest frontend --js --eslint --app --no-src-dir --import-alias "@/*" --no-tailwind --no-turbopack
```

(`--js` — this plan writes plain JavaScript, not TypeScript, so `lib/playback.js` can run directly under `node --test` with no TS loader.)

- [ ] **Step 2: Verify the dev server runs**

Run: `cd frontend && npm run dev`
Expected: starts on `http://localhost:3000`, default Next.js starter page loads in a browser. Stop it (Ctrl+C) once confirmed.

- [ ] **Step 3: Add the API base env var**

Create `frontend/.env.local`:

```
NEXT_PUBLIC_API_BASE=http://localhost:8080
```

- [ ] **Step 4: Commit**

```bash
git add frontend/ ':!frontend/node_modules'
git commit -m "chore(frontend): scaffold Next.js app"
```

---

## Task 2: API client helpers

**Files:**
- Create: `frontend/lib/api.js`

**Interfaces:**
- Produces: `startSession()`, `stopSession()`, `getSessions()`, `getSessionFrames(sessionId)`, `frameUrl(sessionId, sequence)`, `exportUrl(sessionId)`, `setResolution(size)`, `setColorMode(mode)`, `liveWsUrl()`. Consumed by every component in Tasks 4-5.

- [ ] **Step 1: Implement it**

Create `frontend/lib/api.js`:

```js
const API_BASE = process.env.NEXT_PUBLIC_API_BASE ?? 'http://localhost:8080';

export async function startSession() {
  const res = await fetch(`${API_BASE}/session/start`, { method: 'POST' });
  return res.json();
}

export async function stopSession() {
  const res = await fetch(`${API_BASE}/session/stop`, { method: 'POST' });
  return res.json();
}

export async function getSessions() {
  const res = await fetch(`${API_BASE}/sessions`);
  return res.json();
}

export async function getSessionFrames(sessionId) {
  const res = await fetch(`${API_BASE}/session/${sessionId}/frames`);
  return res.json();
}

export function frameUrl(sessionId, sequence) {
  return `${API_BASE}/session/${sessionId}/frame/${sequence}.jpg`;
}

export function exportUrl(sessionId) {
  return `${API_BASE}/session/${sessionId}/export`;
}

export async function setResolution(size) {
  const res = await fetch(`${API_BASE}/resolution`, { method: 'POST', body: size });
  return res.ok;
}

export async function setColorMode(mode) {
  const res = await fetch(`${API_BASE}/colormode`, { method: 'POST', body: mode });
  return res.ok;
}

export function liveWsUrl() {
  return API_BASE.replace(/^http/, 'ws') + '/live';
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/lib/api.js
git commit -m "feat(frontend): add ingest-server API client helpers"
```

---

## Task 3: Playback timing logic

**Files:**
- Create: `frontend/lib/playback.js`
- Test: `frontend/test/playback.test.js`

**Interfaces:**
- Produces: `computeHoldMs(frames, index, options?)`. Consumed by `FramePreviewPlayer.jsx` (Task 5).

- [ ] **Step 1: Write the failing test**

Create `frontend/test/playback.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { computeHoldMs } from '../lib/playback.js';

test('holds for the real gap between two frames', () => {
  const frames = [{ timestamp: 1000 }, { timestamp: 1080 }];
  assert.equal(computeHoldMs(frames, 0), 80);
});

test('clamps a very long gap (e.g. across a backlog catch-up) to maxMs', () => {
  const frames = [{ timestamp: 1000 }, { timestamp: 60000 }];
  assert.equal(computeHoldMs(frames, 0, { minMs: 16, maxMs: 500 }), 500);
});

test('clamps a zero or negative gap up to minMs', () => {
  const frames = [{ timestamp: 1000 }, { timestamp: 1000 }];
  assert.equal(computeHoldMs(frames, 0, { minMs: 16, maxMs: 500 }), 16);
});

test('returns maxMs for the last frame (nothing to gap against)', () => {
  const frames = [{ timestamp: 1000 }, { timestamp: 1080 }];
  assert.equal(computeHoldMs(frames, 1, { minMs: 16, maxMs: 500 }), 500);
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd frontend && node --test test/`
Expected: FAIL — `lib/playback.js` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Create `frontend/lib/playback.js`:

```js
// Given a session's frame metadata (each with a `timestamp`), compute how
// long to hold frames[index] on screen before advancing, based on the real
// capture-time gap. Clamped so a long backlog-catch-up gap (a degraded-mode
// spool flush can span minutes) doesn't stall preview playback for real
// wall-clock time.
export function computeHoldMs(frames, index, { minMs = 16, maxMs = 500 } = {}) {
  if (index >= frames.length - 1) return maxMs;
  const delta = frames[index + 1].timestamp - frames[index].timestamp;
  if (!Number.isFinite(delta) || delta <= 0) return minMs;
  return Math.min(Math.max(delta, minMs), maxMs);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd frontend && node --test test/`
Expected: PASS (4 tests)

- [ ] **Step 5: Commit**

```bash
git add frontend/lib/playback.js frontend/test/playback.test.js
git commit -m "feat(frontend): add preview playback timing logic"
```

---

## Task 4: Live view page

**Files:**
- Create: `frontend/components/LiveView.jsx`
- Create: `frontend/components/ControlsPanel.jsx`
- Create: `frontend/app/page.jsx`

**Interfaces:**
- Consumes: `liveWsUrl`, `startSession`, `stopSession`, `setResolution`, `setColorMode` from `lib/api.js` (Task 2).

- [ ] **Step 1: Implement LiveView**

Create `frontend/components/LiveView.jsx`:

```jsx
'use client';
import { useEffect, useRef, useState } from 'react';
import { liveWsUrl } from '../lib/api';

export default function LiveView() {
  const imgRef = useRef(null);
  const [connected, setConnected] = useState(false);

  useEffect(() => {
    let currentUrl = null;
    const ws = new WebSocket(liveWsUrl());
    ws.binaryType = 'blob';
    ws.onopen = () => setConnected(true);
    ws.onclose = () => setConnected(false);
    ws.onmessage = (event) => {
      if (currentUrl) URL.revokeObjectURL(currentUrl);
      currentUrl = URL.createObjectURL(event.data);
      if (imgRef.current) imgRef.current.src = currentUrl;
    };
    return () => {
      ws.close();
      if (currentUrl) URL.revokeObjectURL(currentUrl);
    };
  }, []);

  return (
    <div>
      <img ref={imgRef} alt="live" style={{ maxWidth: '100%', background: '#000' }} />
      <p>{connected ? 'connected' : 'disconnected'}</p>
    </div>
  );
}
```

- [ ] **Step 2: Implement ControlsPanel**

Create `frontend/components/ControlsPanel.jsx`:

```jsx
'use client';
import { useState } from 'react';
import { startSession, stopSession, setResolution, setColorMode } from '../lib/api';

export default function ControlsPanel() {
  const [recording, setRecording] = useState(false);

  async function toggleRecord() {
    if (recording) await stopSession(); else await startSession();
    setRecording(!recording);
  }

  return (
    <div>
      <button onClick={toggleRecord}>{recording ? 'Stop' : 'Record'}</button>
      <select onChange={(e) => setResolution(e.target.value)} defaultValue="svga">
        <option value="vga">VGA (640x480)</option>
        <option value="svga">SVGA (800x600)</option>
        <option value="uxga">UXGA (1600x1200)</option>
      </select>
      <label>
        <input type="checkbox" onChange={(e) => setColorMode(e.target.checked ? 'gray' : 'color')} />
        Grayscale (smaller frames)
      </label>
    </div>
  );
}
```

- [ ] **Step 3: Implement the page**

Create `frontend/app/page.jsx`:

```jsx
import LiveView from '../components/LiveView';
import ControlsPanel from '../components/ControlsPanel';

export default function Home() {
  return (
    <main>
      <h1>XIAO Cam — Live</h1>
      <LiveView />
      <ControlsPanel />
      <p><a href="/log">Log browser →</a></p>
    </main>
  );
}
```

- [ ] **Step 4: Manual browser test**

With the ingest server (`server/`, previous plan) running on its default ports, run `cd frontend && npm run dev` and open `http://localhost:3000`. Confirm: page loads, "disconnected" shows if the ingest server isn't sending frames, clicking Record calls `/session/start` (check the server's terminal log or Network tab), and the resolution dropdown / grayscale checkbox fire their requests (Network tab shows `POST /resolution` / `POST /colormode`).

- [ ] **Step 5: Commit**

```bash
git add frontend/components/LiveView.jsx frontend/components/ControlsPanel.jsx frontend/app/page.jsx
git commit -m "feat(frontend): add live view page with record and camera controls"
```

---

## Task 5: Log browser + preview + download

**Files:**
- Create: `frontend/components/SessionList.jsx`
- Create: `frontend/components/FramePreviewPlayer.jsx`
- Create: `frontend/app/log/page.jsx`

**Interfaces:**
- Consumes: `getSessions`, `getSessionFrames`, `frameUrl`, `exportUrl` from `lib/api.js` (Task 2), `computeHoldMs` from `lib/playback.js` (Task 3).

- [ ] **Step 1: Implement SessionList**

Create `frontend/components/SessionList.jsx`:

```jsx
'use client';
import { useEffect, useState } from 'react';
import { getSessions } from '../lib/api';

export default function SessionList({ selectedId, onSelect }) {
  const [sessions, setSessions] = useState([]);

  useEffect(() => {
    getSessions().then(setSessions);
  }, []);

  return (
    <ul>
      {sessions.map((s) => (
        <li key={s.id}>
          <button onClick={() => onSelect(s.id)} disabled={s.id === selectedId}>
            Session {s.id} — {new Date(s.started_at).toLocaleString()}
            {s.ended_at ? '' : ' (recording…)'}
          </button>
        </li>
      ))}
    </ul>
  );
}
```

- [ ] **Step 2: Implement FramePreviewPlayer**

Create `frontend/components/FramePreviewPlayer.jsx`:

```jsx
'use client';
import { useEffect, useRef, useState } from 'react';
import { getSessionFrames, frameUrl, exportUrl } from '../lib/api';
import { computeHoldMs } from '../lib/playback';

export default function FramePreviewPlayer({ sessionId }) {
  const [frames, setFrames] = useState([]);
  const [index, setIndex] = useState(0);
  const [playing, setPlaying] = useState(false);
  const timerRef = useRef(null);

  useEffect(() => {
    setIndex(0);
    setPlaying(false);
    getSessionFrames(sessionId).then(setFrames);
  }, [sessionId]);

  useEffect(() => {
    if (!playing || frames.length === 0) return;
    const holdMs = computeHoldMs(frames, index);
    timerRef.current = setTimeout(() => {
      setIndex((i) => (i + 1 >= frames.length ? 0 : i + 1));
    }, holdMs);
    return () => clearTimeout(timerRef.current);
  }, [playing, index, frames]);

  if (frames.length === 0) return <p>No frames in this session.</p>;

  return (
    <div>
      <img src={frameUrl(sessionId, frames[index].sequence)} alt="preview" style={{ maxWidth: '100%' }} />
      <div>
        <button onClick={() => setPlaying((p) => !p)}>{playing ? 'Pause' : 'Play'}</button>
        <input
          type="range"
          min={0}
          max={frames.length - 1}
          value={index}
          onChange={(e) => { setPlaying(false); setIndex(Number(e.target.value)); }}
        />
        <span>{index + 1} / {frames.length}</span>
      </div>
      <a href={exportUrl(sessionId)}>Download session (zip + CSV)</a>
    </div>
  );
}
```

- [ ] **Step 3: Implement the page**

Create `frontend/app/log/page.jsx`:

```jsx
'use client';
import { useState } from 'react';
import SessionList from '../../components/SessionList';
import FramePreviewPlayer from '../../components/FramePreviewPlayer';

export default function LogPage() {
  const [selectedId, setSelectedId] = useState(null);

  return (
    <main>
      <h1>Log browser</h1>
      <SessionList selectedId={selectedId} onSelect={setSelectedId} />
      {selectedId && <FramePreviewPlayer sessionId={selectedId} />}
      <p><a href="/">← Live view</a></p>
    </main>
  );
}
```

- [ ] **Step 4: Manual browser test**

With the ingest server running and at least one session recorded (use `server/test/manual-fake-esp32.js` from the ingest-server plan to populate one without needing real hardware), open `http://localhost:3000/log`. Confirm: the session appears in the list, clicking it loads and plays back its frames, the scrubber lets you jump to any frame, and the download link produces a zip containing the JPEGs plus `metadata.csv`.

- [ ] **Step 5: Commit**

```bash
git add frontend/components/SessionList.jsx frontend/components/FramePreviewPlayer.jsx frontend/app/log/page.jsx
git commit -m "feat(frontend): add log browser with session preview and export"
```

---

## Self-Review Notes

- **Spec coverage:** live view ✓, Record button (session start/stop) ✓, resolution/grayscale controls ✓, log browser ✓, preview playback ✓, download (zip+CSV) ✓.
- **Type consistency:** every component imports exactly the function names/signatures `lib/api.js` (Task 2) exports; `FramePreviewPlayer`'s use of `computeHoldMs(frames, index)` matches the signature defined and tested in Task 3.
- **No placeholders:** every step has complete component code; manual browser test steps spell out exactly what to click and what to expect, per this project's established UI-testing convention (no headless component test framework introduced, to avoid adding tooling the rest of this small app doesn't otherwise need).
