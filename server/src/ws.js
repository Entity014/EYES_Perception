import { WebSocketServer } from 'ws';

export function createLiveHub(httpServer) {
  const webSocketServer = new WebSocketServer({ server: httpServer, path: '/live' });
  return {
    broadcast(payload) {
      for (const client of webSocketServer.clients) {
        // A slow browser otherwise accumulates every JPEG and displays them
        // seconds later. Dropping queued frames keeps this a live view.
        if (client.readyState === client.OPEN && client.bufferedAmount === 0) {
          client.send(payload, { binary: true });
        }
      }
    },
    close() {
      webSocketServer.close();
    },
  };
}
