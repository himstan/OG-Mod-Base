#pragma once

#include <cstdint>

#pragma pack(push, 1)

const int DISCOVERY_PORT = 3001;
const char* const DISCOVERY_MAGIC = "OG_MP_DISCOVERY";

enum class MultiplayerStatus : uint8_t {
  IDLE = 0,
  SEARCHING = 1,
  FOUND = 2,
  CONNECTING = 3,
  CONNECTED_LOBBY = 4, // Both connected, waiting in menu
  GAME_STARTING = 5,   // Host/Client is loading the save/level
  IN_GAME = 6,         // Fully spawned and playing
  RECONNECTING = 7,    // Connection lost, trying to restore
  HOST_LEFT = 8,       // Host has explicitly left the session
  FAILED = 255
};

enum class PacketType : uint8_t {
  STATE_UPDATE = 0,
  EVENT_JOIN = 1,
  EVENT_LEAVE = 2,
  EVENT_GAME = 3,
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
  uint8_t status;
  float x, y, z, angle;
  uint16_t anim;
  float anim_frame;
  uint32_t level_hash;
  uint32_t riding;
  int32_t sidekick_anim;
  float sidekick_frame;
  uint64_t clock;
  float last_anim_frame;
  float last_sidekick_frame;
};

struct PacketGameEvent {
  PacketHeader header;
  uint8_t raw_data[48];
};

struct WorldEventData {
  uint32_t actor_id;
};

struct SceneEventData {
  uint32_t state;
  char scene_name[32];
  uint32_t event_id;
};

struct MPEnemyState {
  uint32_t actor_id;
  float x, y, z;
  float quat_x, quat_y, quat_z, quat_w;
  int32_t anim_index;
  float anim_frame;
  float last_anim_frame;
  int32_t hp;
  uint32_t state;
  uint32_t focus_aid;
  uint8_t attack_flag;
  uint8_t owner;
  uint8_t is_aggro;
  uint8_t pad[5];
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
};

#pragma pack(pop)
