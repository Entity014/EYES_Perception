import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createApiServer } from './api.js';
import { openDb } from './db.js';
import { startIngestServer } from './ingest.js';
import { createLiveHub } from './ws.js';

const directory = path.dirname(fileURLToPath(import.meta.url));
const dataDir = path.join(directory, '..', 'data');
fs.mkdirSync(dataDir, { recursive: true });
const db = openDb(path.join(dataDir, 'log.sqlite'));
const apiServer = createApiServer({
  db,
  dataDir,
  esp32Host: process.env.ESP32_HOST ?? 'xiao-cam.local',
  esp32Port: Number(process.env.ESP32_PORT ?? 80),
});
const liveHub = createLiveHub(apiServer);
const ingestServer = startIngestServer({
  port: Number(process.env.INGEST_PORT ?? 9000),
  db,
  dataDir,
  broadcastLive: (payload) => liveHub.broadcast(payload),
});
const apiPort = Number(process.env.API_PORT ?? 8080);

apiServer.listen(apiPort, () => {
  console.log(`API + live WS on :${apiPort}, TCP ingest on :${ingestServer.address().port}`);
});

function shutdown() {
  ingestServer.close();
  apiServer.close(() => {
    liveHub.close();
    db.close();
  });
}

process.once('SIGINT', shutdown);
process.once('SIGTERM', shutdown);