const express = require('express');
const http = require('http');
const path = require('path');
const WebSocket = require('ws');

const { WebSocketServer } = WebSocket;

const app = express();
app.use(express.static(path.join(__dirname, 'public')));

app.get('/health', (req, res) => {
  res.json({
    ok: true,
    sourceConnected: !!sourceSocket,
    listeners: listeners.size
  });
});

const server = http.createServer(app);
const wss = new WebSocketServer({ server, path: '/ws' });

let sourceSocket = null;
const listeners = new Set();

function broadcastJSON(obj) {
  const msg = JSON.stringify(obj);
  for (const ws of listeners) {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(msg);
    }
  }
}

wss.on('connection', (ws, req) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const role = url.searchParams.get('role') === 'source' ? 'source' : 'listener';

  ws.role = role;
  ws.isAlive = true;

  ws.on('pong', () => {
    ws.isAlive = true;
  });

  if (role === 'source') {
    if (sourceSocket && sourceSocket.readyState === WebSocket.OPEN) {
      sourceSocket.close(1012, 'A newer source connected.');
    }
    sourceSocket = ws;
    broadcastJSON({ type: 'status', streaming: true });
    ws.send(JSON.stringify({ type: 'hello', role: 'source' }));
  } else {
    listeners.add(ws);
    ws.send(JSON.stringify({
      type: 'hello',
      role: 'listener',
      streaming: !!sourceSocket
    }));
  }

  ws.on('message', (data, isBinary) => {
    if (role !== 'source' || !isBinary) return;

    for (const client of listeners) {
      if (client.readyState === WebSocket.OPEN) {
        client.send(data, { binary: true });
      }
    }
  });

  ws.on('close', () => {
    if (role === 'source' && sourceSocket === ws) {
      sourceSocket = null;
      broadcastJSON({ type: 'status', streaming: false });
    }
    listeners.delete(ws);
  });

  ws.on('error', () => {});
});

// Keep connections healthy
setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.isAlive) {
      ws.terminate();
      continue;
    }
    ws.isAlive = false;
    ws.ping();
  }
}, 30000);

const port = process.env.PORT || 10000;
server.listen(port, '0.0.0.0', () => {
  console.log(`Listening on ${port}`);
});
