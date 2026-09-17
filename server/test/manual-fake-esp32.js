import net from 'node:net';

const ingestPort = Number(process.env.INGEST_PORT ?? 9000);
const apiPort = Number(process.env.API_PORT ?? 8080);

function encodeFrame(flag, sequence, payload) {
  const header = Buffer.alloc(11);
  header[0] = 0xaa; header[1] = 0x55; header[2] = flag;
  header.writeUInt32LE(sequence, 3);
  header.writeUInt32LE(payload.length, 7);
  return Buffer.concat([header, payload]);
}

const startResponse = await fetch(`http://localhost:${apiPort}/session/start`, { method: 'POST' });
const { id: sessionId } = await startResponse.json();
const socket = net.connect(ingestPort, 'localhost');
await new Promise((resolve, reject) => {
  socket.once('connect', resolve);
  socket.once('error', reject);
});

for (let sequence = 0; sequence < 20; sequence++) {
  socket.write(encodeFrame(sequence < 10 ? 0 : 1, sequence, Buffer.from(`fake-jpeg-${sequence}`)));
}
socket.end();
await new Promise((resolve) => socket.once('close', resolve));

await fetch(`http://localhost:${apiPort}/session/stop`, { method: 'POST' });
const frames = await (await fetch(`http://localhost:${apiPort}/session/${sessionId}/frames`)).json();
if (frames.length !== 20) throw new Error(`expected 20 frames, stored ${frames.length}`);
console.log(`PASS: stored ${frames.length}/20 frames with no gaps`);