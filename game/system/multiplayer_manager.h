#pragma once

#include "multiplayer_types.h"
#include "multiplayer_protocol.h"
#include "enet/enet.h"

class MultiplayerManager {
 public:
  static void setup_host(MultiplayerData& data);
  static void setup_client(MultiplayerData& data, const char* ip);
  static void disconnect(MultiplayerData& data);

  template <typename T>
  static void broadcast(MultiplayerData& data,
                        int channel,
                        const T& packet_data,
                        ENetPacketFlag flags) {
    if (!data.host)
      return;
    ENetPacket* packet = enet_packet_create(&packet_data, sizeof(T), flags);
    if (data.local_role == 0) {
      enet_host_broadcast(data.host, channel, packet);
    } else if (data.server_peer) {
      enet_peer_send(data.server_peer, channel, packet);
    }
  }

  static void send_to_peer(ENetPeer* peer,
                           int channel,
                           const void* packet_data,
                           size_t size,
                           ENetPacketFlag flags);
};
