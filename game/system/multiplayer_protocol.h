#pragma once

#include <cstdint>

#pragma pack(push, 1)

enum class PacketType : uint8_t {
  STATE_UPDATE = 0,
  EVENT_JOIN = 1,
  EVENT_LEAVE = 2,
  EVENT_WORLD = 3,
  FULL_SYNC = 4,
  ENEMY_SYNC = 5
};

struct PacketHeader {
  PacketType type;
  uint32_t sequenceNum;
};

struct PacketPlayerState {
  PacketHeader header;
  uint32_t netId;
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
};

struct PacketWorldEvent {
  PacketHeader header;
  uint32_t event_type;
  uint32_t actor_id;
};

struct MPEnemyState {
  uint32_t actor_id;
  float x, y, z;
  float quat_x, quat_y, quat_z, quat_w;
  int32_t anim_index;
  float anim_frame;
  int32_t hp;
  uint32_t state;
  uint32_t focus_aid;
  uint8_t attack_flag;
  uint8_t pad[11];
};

struct PacketEnemySync {
  PacketHeader header;
  uint32_t count;
  MPEnemyState enemies[24];
};

struct PacketFullSync {
  PacketHeader header;
  float money;
  float gems;
  float skill;
  float x, y, z;
  uint32_t host_task;
  uint32_t host_node;
  char host_continue[32];
  uint8_t task_mask[64];
  uint32_t sync_aids_count;
  uint32_t riding;
  uint32_t sync_aids[128];
  int32_t sidekick_anim;
  float sidekick_frame;
  uint64_t clock;
  char scene_name[32];
  uint32_t scene_active;
};

#pragma pack(pop)
