#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <string>

#include "multiplayer_protocol.h"

struct RemoteEntityState {
  float x, y, z, angle;
  uint16_t anim;
  float anim_frame;
  uint32_t level_hash;
  uint32_t riding;
  int32_t sidekick_anim;
  float sidekick_frame;
  uint64_t clock;
  char scene_name[32];
  uint32_t scene_active;
  uint32_t last_sequence_num = 0;
};

struct MultiplayerInfoGOAL {
  float local_x, local_y, local_z, local_angle;
  float remote_x, remote_y, remote_z, remote_angle;
  uint32_t remote_id;
  int32_t remote_role;
  int32_t remote_anim;
  int32_t local_anim;
  uint32_t local_level;
  uint32_t remote_level;
  int32_t remote_status;
  int32_t local_role;
  float local_anim_frame;
  float remote_anim_frame;
  uint32_t local_packet_id;
  uint32_t remote_packet_id;
  uint32_t in_event_type;
  uint32_t in_event_aid;
  uint32_t in_event_seq;
  uint32_t out_event_type;
  uint32_t out_event_aid;
  uint32_t out_event_seq;
  float host_money;
  float host_gems;
  float host_skill;
  float client_sync_money;
  float client_sync_gems;
  float client_sync_skill;
  uint32_t client_sync_flag;
  uint32_t host_task;
  uint32_t host_node;
  char host_continue[32];
  uint8_t task_mask[64];
  uint32_t sync_aids_count;
  uint32_t riding;
  uint32_t enemy_count;
  uint8_t enemies_pad[8];
  MPEnemyState enemies[24];
  uint64_t player_procs[2];
  uint32_t sync_aids[128];
  int32_t sidekick_anim;
  float sidekick_frame;
  uint8_t sync_clock_pad[8];
  uint64_t sync_clock;
  uint8_t sync_scene_name[32];
  uint32_t sync_scene_active;
  uint8_t pad[20];
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
  std::vector<PacketWorldEvent> inbound_events;

  // New fields for joining/searching
  std::atomic<int> join_status{0}; // 0: idle, 1: searching, 2: found, 3: connecting, 4: connected, -1: failed
  std::string found_ip = "";
  std::atomic<bool> stop_search{false};
};
