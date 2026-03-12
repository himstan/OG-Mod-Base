#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <string>
#include <thread>

#include "multiplayer_protocol.h"

struct RemoteEntityState {
  uint8_t status;
  float x, y, z, angle;
  uint16_t anim;
  float anim_frame;
  float last_anim_frame;
  uint32_t level_hash;
  uint32_t riding;
  int32_t sidekick_anim;
  float sidekick_frame;
  float last_sidekick_frame;
  uint64_t clock;
  uint32_t last_sequence_num = 0;
};

struct MPEvent {
  uint32_t etype;
  uint8_t data[44];
};

struct MPEventBufferGOAL {
  uint32_t out_count;
  uint8_t pad1[12];
  MPEvent out_events[16];
  uint32_t in_count;
  uint8_t pad2[12];
  MPEvent in_events[16];
};

struct RemotePlayerInfoGOAL {
  float x, y, z, angle;
  uint32_t id;
  int32_t role;
  int32_t anim;
  float anim_frame;
  uint32_t level;
  int32_t status;
  uint32_t packet_id;
  uint32_t riding;
  int32_t sidekick_anim;
  float sidekick_frame;
  uint64_t clock;
  float last_anim_frame;
  float last_sidekick_frame;
  uint8_t pad[8];
};

struct LocalPlayerInfoGOAL {
  float x, y, z, angle;
  int32_t role;
  int32_t anim;
  float anim_frame;
  uint32_t level;
  uint32_t packet_id;
  uint32_t riding;
  int32_t sidekick_anim;
  float sidekick_frame;
  uint64_t clock;
  float last_anim_frame;
  float last_sidekick_frame;
  uint8_t pad[4];
  // Global World Sync (Outgoing)
  float money;
  float gems;
  float skill;
  // Global World Sync (Incoming)
  float sync_money;
  float sync_gems;
  float sync_skill;
  uint32_t sync_flag;
  uint32_t host_task;
  uint32_t host_node;
  uint8_t host_continue[32];
  uint8_t task_mask[64];
  uint32_t sync_aids_count;
  uint32_t sync_aids[128];
  // AI / Enemy Sync
  uint32_t enemy_count;
  uint8_t pad_enemy[12];
  MPEnemyState enemies[24];
  uint64_t player_procs[2];
};

struct MultiplayerData {
  bool initialized = false;
  bool enet_initialized = false;
  struct _ENetHost* host = nullptr;
  struct _ENetPeer* server_peer = nullptr;  // Only used if we are a client
  int local_role = -1;
  uint32_t local_net_id = 0;
  uint32_t sequence_num = 0;
  uint32_t last_out_event_seq = 0;

  std::unordered_map<uint32_t, RemoteEntityState> remote_entities;
  std::vector<PacketGameEvent> inbound_events;

  // New fields for joining/searching
  std::atomic<int> join_status{0}; // 0: idle, 1: searching, 2: found, 3: connecting, 4: connected, -1: failed
  std::string found_ip = "";
  std::atomic<bool> stop_search{false};

  // Discovery / Hosting responder
  std::thread discovery_thread;
  std::atomic<bool> host_discovery_active{false};
  std::atomic<bool> pending_full_sync{false};

  // Reconnection tracking
  uint32_t last_receive_time = 0;
  int pre_reconnect_status = 0;
};
