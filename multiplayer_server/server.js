const dgram = require('dgram');
const server = dgram.createSocket('udp4');

const PORT = 8080;
const clients = new Map(); // Map of remoteAddress:port -> clientInfo

server.on('error', (err) => {
  console.error(`Server error:
${err.stack}`);
  server.close();
});

server.on('message', (msg, rinfo) => {
  const clientKey = `${rinfo.address}:${rinfo.port}`;
  
  // Register/Update client
  if (!clients.has(clientKey)) {
    console.log(`New client connected: ${clientKey}`);
  }
  clients.set(clientKey, { 
    rinfo, 
    lastSeen: Date.now() 
  });

  // Simple broadcast to all OTHER clients
  // The payload is expected to be [PlayerID, X, Y, Z, Angle] as floats (20 bytes)
  for (const [key, client] of clients) {
    if (key !== clientKey) {
      server.send(msg, client.rinfo.port, client.rinfo.address, (err) => {
        if (err) {
          console.error(`Error sending to ${key}:`, err);
          clients.delete(key);
        }
      });
    }
  }

  // Cleanup stale clients (inactive for > 10 seconds)
  const now = Date.now();
  for (const [key, client] of clients) {
    if (now - client.lastSeen > 10000) {
      clients.delete(key);
      console.log(`Client ${key} timed out.`);
    }
  }
});

server.on('listening', () => {
  const address = server.address();
  console.log(`UDP Relay Server listening on ${address.address}:${address.port}`);
});

server.bind(PORT);
