#include "multiplayer_manager.h"
#include "common/log/log.h"
#include "enet/enet.h"

void MultiplayerManager::setup_host(MultiplayerData& data) {
  if (data.host)
    disconnect(data);

  if (!data.enet_initialized) {
    if (enet_initialize() != 0)
      return;
    data.enet_initialized = true;
  }

  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = 3000;

  data.host = enet_host_create(&address, 32, 2, 0, 0);
  if (data.host) {
    lg::info("[Multiplayer] Listen server started on port 3000.");
    data.local_role = 0;
    data.local_net_id = 0;
    data.join_status = 4;
    data.initialized = true;
  }
}

void MultiplayerManager::setup_client(MultiplayerData& data, const char* ip) {
  if (data.host)
    disconnect(data);

  if (!data.enet_initialized) {
    if (enet_initialize() != 0)
      return;
    data.enet_initialized = true;
  }

  data.host = enet_host_create(NULL, 1, 2, 0, 0);
  if (data.host) {
    ENetAddress server_address;
    enet_address_set_host(&server_address, ip);
    server_address.port = 3000;

    data.server_peer = enet_host_connect(data.host, &server_address, 2, 0);
    if (data.server_peer) {
      lg::info("[Multiplayer] Client connecting to {}:3000...", ip);
      data.local_role = 1;
      data.local_net_id = 1;
      if (std::string(ip) == "127.0.0.1") {
        data.join_status = 4; // Skip searching/connecting states for local
      } else {
        data.join_status = 3;  // Connecting
      }
      data.initialized = true;
    } else {
      enet_host_destroy(data.host);
      data.host = nullptr;
    }
  }
}

void MultiplayerManager::disconnect(MultiplayerData& data) {
  if (!data.initialized)
    return;

  if (data.host) {
    if (data.local_role == 1 && data.server_peer) {
      enet_peer_disconnect_now(data.server_peer, 0);
    }
    enet_host_destroy(data.host);
    data.host = nullptr;
  }

  data.initialized = false;
  lg::info("[Multiplayer] Disconnected.");
}

void MultiplayerManager::send_to_peer(ENetPeer* peer,
                                      int channel,
                                      const void* packet_data,
                                      size_t size,
                                      ENetPacketFlag flags) {
  ENetPacket* packet = enet_packet_create(packet_data, size, flags);
  enet_peer_send(peer, channel, packet);
}
