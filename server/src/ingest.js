import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { FrameParser } from './protocol.js';

export function shouldBroadcastLive(frame) {
  return frame.flag === 0;
}

// Frames flagged as backlog were captured before the current connection was
// healthy.  They are useful for transport recovery, but must never become the
// beginning of a newly started recording.
export function shouldStoreInSession(frame) {
  return frame.flag === 1;
}

export function startIngestServer({ port, db, dataDir, broadcastLive }) {
  const server = net.createServer((socket) => {
    const parser = new FrameParser();
    socket.on('data', (chunk) => {
      for (const frame of parser.push(chunk)) {
        if (shouldBroadcastLive(frame)) broadcastLive(frame.payload);
        if (frame.flag === 2) {
          const session = db.getUploadingSession();
          if (session) db.completeUpload(session.id);
          continue;
        }
        const session = db.getUploadingSession();
        if (!session || !shouldStoreInSession(frame)) continue;

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
    socket.on('error', () => {});
  });

  server.listen(port);
  return server;
}
