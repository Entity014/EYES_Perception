import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import archiver from 'archiver';
import { Readable } from 'node:stream';
import { pipeline } from 'node:stream/promises';
import { createAviVideo, createSessionVideo } from './video.js';

function sendJson(response, status, body) {
  response.writeHead(status, { 'Content-Type': 'application/json' });
  response.end(JSON.stringify(body));
}

function setCorsHeaders(request, response) {
  response.setHeader('Access-Control-Allow-Origin', request.headers.origin ?? '*');
  response.setHeader('Access-Control-Allow-Methods', 'GET,POST,OPTIONS');
  response.setHeader('Access-Control-Allow-Headers', 'Content-Type');
}

async function readBody(request) {
  const chunks = [];
  for await (const chunk of request) chunks.push(chunk);
  return Buffer.concat(chunks).toString('utf8');
}

async function forwardToEsp32(host, port, route, body) {
  const response = await fetch(`http://${host}:${port}${route}`, {
    method: 'POST',
    headers: { 'Content-Type': 'text/plain' },
    body,
  });
  return response.ok;
}

async function toggleEsp32Recording(host, port) {
  try {
    return await forwardToEsp32(host, port, '/record', '');
  } catch {
    return false;
  }
}

const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function waitForCameraStop(host, port) {
  for (let attempt = 0; attempt < 80; attempt += 1) {
    try {
      const response = await fetch(`http://${host}:${port}/status`);
      if (response.ok && !(await response.json()).recording) return true;
    } catch {}
    await pause(125);
  }
  return false;
}

async function downloadCameraVideo(host, port, targetPath) {
  const response = await fetch(`http://${host}:${port}/video.avi`);
  if (!response.ok || !response.body) throw new Error(`camera video download failed (${response.status})`);
  await pipeline(Readable.fromWeb(response.body), fs.createWriteStream(targetPath));
}

export function createApiServer({ db, dataDir, esp32Host, esp32Port }) {
  return http.createServer(async (request, response) => {
    try {
      setCorsHeaders(request, response);
      if (request.method === 'OPTIONS') {
        response.writeHead(204);
        return response.end();
      }
      const url = new URL(request.url, 'http://localhost');
      if (request.method === 'POST' && url.pathname === '/session/start') {
        if (db.getUploadingSession()) return sendJson(response, 409, { error: 'previous session is still uploading' });
        const cameraRecording = await toggleEsp32Recording(esp32Host, esp32Port);
        if (!cameraRecording) return sendJson(response, 502, { error: 'camera recording toggle failed' });
        // Do not open the database session until the camera has acknowledged
        // the transition. Frames arriving during the transition are discarded.
        const session = db.createSession();
        return sendJson(response, 200, { ...session, cameraRecording });
      }
      if (request.method === 'POST' && url.pathname === '/session/stop') {
        const session = db.getOpenSession();
        if (!session) return sendJson(response, 400, { error: 'no open session' });
        const cameraRecording = await toggleEsp32Recording(esp32Host, esp32Port);
        if (!cameraRecording || !(await waitForCameraStop(esp32Host, esp32Port))) {
          return sendJson(response, 502, { error: 'camera did not stop recording' });
        }
        const sessionDir = path.join(dataDir, String(session.id));
        fs.mkdirSync(sessionDir, { recursive: true });
        await downloadCameraVideo(esp32Host, esp32Port, path.join(sessionDir, 'session.avi'));
        db.endSession(session.id);
        return sendJson(response, 200, { id: session.id, cameraRecording, uploaded: true });
      }
      if (request.method === 'GET' && url.pathname === '/sessions') {
        return sendJson(response, 200, db.getSessions());
      }

      const frameListMatch = url.pathname.match(/^\/session\/(\d+)\/frames$/);
      if (request.method === 'GET' && frameListMatch) {
        return sendJson(response, 200, db.getFramesForSession(Number(frameListMatch[1])));
      }

      const frameFileMatch = url.pathname.match(/^\/session\/(\d+)\/frame\/(\d+)\.jpg$/);
      if (request.method === 'GET' && frameFileMatch) {
        const [, sessionId, sequence] = frameFileMatch;
        const filePath = path.join(dataDir, sessionId, `frame_${sequence}.jpg`);
        if (!fs.existsSync(filePath)) return sendJson(response, 404, { error: 'not found' });
        response.writeHead(200, {
          'Content-Type': 'image/jpeg',
          'Access-Control-Allow-Origin': request.headers.origin ?? '*',
        });
        return fs.createReadStream(filePath).pipe(response);
      }

      const exportMatch = url.pathname.match(/^\/session\/(\d+)\/export$/);
      if (request.method === 'GET' && exportMatch) {
        const sessionId = Number(exportMatch[1]);
        const frames = db.getFramesForSession(sessionId);
        response.writeHead(200, {
          'Content-Type': 'application/zip',
          'Content-Disposition': `attachment; filename="session_${sessionId}.zip"`,
          'Access-Control-Allow-Origin': request.headers.origin ?? '*',
        });
        const archive = archiver('zip');
        archive.on('error', (error) => response.destroy(error));
        archive.pipe(response);
        const csvLines = ['sequence,timestamp,source'];
        for (const frame of frames) {
          archive.file(path.join(dataDir, String(sessionId), frame.path), { name: frame.path });
          csvLines.push(`${frame.sequence},${frame.timestamp},${frame.source}`);
        }
        archive.append(`${csvLines.join('\n')}\n`, { name: 'metadata.csv' });
        return archive.finalize();
      }

      const videoMatch = url.pathname.match(/^\/session\/(\d+)\/(video|preview)\.mp4$/);
      if (request.method === 'GET' && videoMatch) {
        const sessionId = Number(videoMatch[1]);
        const inlinePreview = videoMatch[2] === 'preview';
        const session = db.getSession(sessionId);
        if (!session) return sendJson(response, 404, { error: 'session not found' });
        if (session.upload_state !== 'complete') return sendJson(response, 409, { error: 'session is still uploading' });
        const videoPath = path.join(dataDir, String(sessionId), 'session.mp4');
        try {
          const aviPath = path.join(dataDir, String(sessionId), 'session.avi');
          if (fs.existsSync(aviPath)) await createAviVideo({ aviPath, outputPath: videoPath });
          else await createSessionVideo({ dataDir, sessionId, frames: db.getFramesForSession(sessionId) });
          response.writeHead(200, {
            'Content-Type': 'video/mp4',
            'Content-Disposition': `${inlinePreview ? 'inline' : 'attachment'}; filename="session_${sessionId}.mp4"`,
            'Access-Control-Allow-Origin': request.headers.origin ?? '*',
          });
          return fs.createReadStream(videoPath).pipe(response);
        } catch (error) {
          return sendJson(response, 500, { error: `video export failed: ${error.message}` });
        }
      }

      const aviMatch = url.pathname.match(/^\/session\/(\d+)\/video\.avi$/);
      if (request.method === 'GET' && aviMatch) {
        const filePath = path.join(dataDir, aviMatch[1], 'session.avi');
        if (!fs.existsSync(filePath)) return sendJson(response, 404, { error: 'video not found' });
        response.writeHead(200, { 'Content-Type': 'video/x-msvideo', 'Content-Disposition': `attachment; filename="session_${aviMatch[1]}.avi"` });
        return fs.createReadStream(filePath).pipe(response);
      }

      if (request.method === 'POST' && (url.pathname === '/resolution' || url.pathname === '/colormode' || url.pathname === '/brightness')) {
        const ok = await forwardToEsp32(esp32Host, esp32Port, url.pathname, await readBody(request));
        return sendJson(response, ok ? 200 : 502, { ok });
      }
      return sendJson(response, 404, { error: 'not found' });
    } catch (error) {
      return sendJson(response, 500, { error: String(error) });
    }
  });
}
