const dgram = require('dgram');
const server = dgram.createSocket('udp4');

const PORT = 8080;
const clients = new Map(); // Map of clientID (u32) -> clientInfo

// Packet Types
const PACKET_JOIN = 0;
const PACKET_LEAVE = 1;
const PACKET_SYNC = 2;

server.on('error', (err) => {
  console.error(`[Server Error] ${err.stack}`);
  server.close();
});

server.on('message', (msg, rinfo) => {
  if (msg.length < 5) return;

  const type = msg.readUInt8(0);
  const senderID = msg.readUInt32LE(1);
  const clientKey = `${rinfo.address}:${rinfo.port}`;

  // LEAVE Packet (1): Immediate disconnection
  if (type === PACKET_LEAVE) {
    if (clients.has(senderID)) {
      console.log(`[Server] Client ${senderID} disconnected explicitly.`);
      clients.delete(senderID);

      // Broadcast to others
      for (const [id, other] of clients) {
        server.send(msg, other.rinfo.port, other.rinfo.address);
      }
    }
    return;
  }

  // JOIN Packet (0): Handle registration and role assignment
  if (type === PACKET_JOIN) {
    let client = clients.get(senderID);
    if (!client) {
      const role = clients.size; // 0 for Jak, 1 for Daxter
      console.log(`[Server] New Client ${senderID} joined from ${clientKey} (Assigned Role: ${role})`);
      
      client = { rinfo, role, lastSeen: Date.now() };
      clients.set(senderID, client);

      // 1. Tell the NEW client about all EXISTING players
      for (const [id, other] of clients) {
        if (id !== senderID) {
          const existingJoin = Buffer.alloc(9);
          existingJoin.writeUInt8(PACKET_JOIN, 0);
          existingJoin.writeUInt32LE(id, 1);
          existingJoin.writeInt32LE(other.role, 5);
          server.send(existingJoin, rinfo.port, rinfo.address);
        }
      }

      // 2. Broadcast the NEW player to all OTHER clients
      const joinBroadcast = Buffer.alloc(9);
      joinBroadcast.writeUInt8(PACKET_JOIN, 0);
      joinBroadcast.writeUInt32LE(senderID, 1);
      joinBroadcast.writeInt32LE(role, 5);
      for (const [id, other] of clients) {
        if (id !== senderID) {
          server.send(joinBroadcast, other.rinfo.port, other.rinfo.address);
        }
      }
    }

    // Always send JOIN confirmation back to sender (handling retries)
    const joinResponse = Buffer.alloc(9);
    joinResponse.writeUInt8(PACKET_JOIN, 0);
    joinResponse.writeUInt32LE(senderID, 1);
    joinResponse.writeInt32LE(client.role, 5);
    server.send(joinResponse, rinfo.port, rinfo.address);
    return;
  }

  // SYNC Packet (2): Broadcast to all OTHER clients
  if (type === PACKET_SYNC) {
    const client = clients.get(senderID);
    if (!client || msg.length < 29) return;
    
    client.lastSeen = Date.now();
    
    // AUTHORITATIVE ROLE OVERWRITE (header:5 + pos:16 + anim:4 = offset 25)
    msg.writeInt32LE(client.role, 25);

    for (const [id, other] of clients) {
      if (id !== senderID) {
        server.send(msg, other.rinfo.port, other.rinfo.address);
      }
    }
    return;
  }
});

// Periodic cleanup and LEAVE broadcast
setInterval(() => {
  const now = Date.now();
  for (const [id, client] of clients) {
    if (now - client.lastSeen > 10000) {
      console.log(`[Server] Client ${id} timed out.`);
      clients.delete(id);

      const leavePacket = Buffer.alloc(5);
      leavePacket.writeUInt8(PACKET_LEAVE, 0);
      leavePacket.writeUInt32LE(id, 1);

      for (const [otherID, otherClient] of clients) {
        server.send(leavePacket, otherClient.rinfo.port, otherClient.rinfo.address);
      }
    }
  }
}, 5000);

server.on('listening', () => {
  const address = server.address();
  console.log(`UDP Relay Server listening on ${address.address}:${address.port}`);
});

server.bind(PORT);
