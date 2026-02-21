const dgram = require('dgram');
const server = dgram.createSocket('udp4');

const PORT = 8080;
const clients = new Map(); // Map of remoteAddress:port -> clientInfo

server.on('error', (err) => {
  console.error(`[Server Error] ${err.stack}`);
  server.close();
});

server.on('message', (msg, rinfo) => {
  const clientKey = `${rinfo.address}:${rinfo.port}`;
  
  // Register/Update client
  if (!clients.has(clientKey)) {
    console.log(`[Server] New connection from ${clientKey}`);
  }
  
  // LOG EVERY PACKET FOR DEBUGGING
  // console.log(`[Server] Packet from ${clientKey} (len: ${msg.length})`);

  clients.set(clientKey, { 
    rinfo, 
    lastSeen: Date.now() 
  });

  // Simple broadcast to all OTHER clients
  for (const [key, client] of clients) {
    if (key !== clientKey) {
      server.send(msg, client.rinfo.port, client.rinfo.address, (err) => {
        if (err) {
          console.error(`[Server] Error sending to ${key}:`, err);
          clients.delete(key);
        }
      });
    }
  }
});

// Periodic cleanup and status
setInterval(() => {
    const now = Date.now();
    for (const [key, client] of clients) {
        if (now - client.lastSeen > 10000) {
            console.log(`[Server] Client timed out: ${key}`);
            clients.delete(key);
        }
    }
    if (clients.size > 0) {
        console.log(`[Server] Active Clients: ${clients.size}`);
    }
}, 5000);

server.on('listening', () => {
  const address = server.address();
  console.log(`UDP Relay Server listening on ${address.address}:${address.port}`);
});

server.bind(PORT);
