#include "multiplayer.h"
#include "multiplayer_manager.h"
#include "multiplayer_scanner.h"
#include "multiplayer_types.h"
#include "multiplayer_protocol.h"

#include "common/log/log.h"
#include "game/kernel/common/kmachine.h"
#include "game/kernel/jak2/kscheme.h"
#include <cstring>

namespace {
MultiplayerData gMultiplayerData;

void handle_packet_receive(MultiplayerInfoGOAL* info) {
  ENetEvent event;
  while (enet_host_service(gMultiplayerData.host, &event, 0) > 0) {
    switch (event.type) {
      case ENET_EVENT_TYPE_RECEIVE: {
        if (event.packet->dataLength >= sizeof(PacketHeader)) {
          PacketHeader* header = (PacketHeader*)event.packet->data;

          if (header->type == PacketType::STATE_UPDATE &&
              event.packet->dataLength == sizeof(PacketPlayerState)) {
            PacketPlayerState* state = (PacketPlayerState*)event.packet->data;
            auto& entity = gMultiplayerData.remote_entities[state->netId];
            if (state->header.sequenceNum > entity.last_sequence_num) {
              entity.x = state->x;
              entity.y = state->y;
              entity.z = state->z;
              entity.angle = state->angle;
              entity.anim = state->anim;
              entity.anim_frame = state->anim_frame;
              entity.level_hash = state->level_hash;
              entity.riding = state->riding;
              entity.sidekick_anim = state->sidekick_anim;
              entity.sidekick_frame = state->sidekick_frame;
              entity.clock = state->clock;
              entity.last_sequence_num = state->header.sequenceNum;
            }
          } else if (header->type == PacketType::EVENT_WORLD &&
                     event.packet->dataLength == sizeof(PacketWorldEvent)) {
            gMultiplayerData.inbound_events.push_back(*(PacketWorldEvent*)event.packet->data);
          } else if (header->type == PacketType::ENEMY_SYNC &&
                     event.packet->dataLength == sizeof(PacketEnemySync)) {
            PacketEnemySync* enemy_sync = (PacketEnemySync*)event.packet->data;
            if (gMultiplayerData.local_role == 1) {
              info->enemy_count = enemy_sync->count;
              memcpy(info->enemies, enemy_sync->enemies, sizeof(MPEnemyState) * 24);
            }
          } else if (header->type == PacketType::FULL_SYNC &&
                     event.packet->dataLength == sizeof(PacketFullSync)) {
            PacketFullSync* full_sync = (PacketFullSync*)event.packet->data;
            info->client_sync_money = full_sync->money;
            info->client_sync_gems = full_sync->gems;
            info->client_sync_skill = full_sync->skill;
            info->remote_x = full_sync->x;
            info->remote_y = full_sync->y;
            info->remote_z = full_sync->z;
            info->host_task = full_sync->host_task;
            info->host_node = full_sync->host_node;
            memcpy(info->host_continue, full_sync->host_continue, 32);
            memcpy(info->task_mask, full_sync->task_mask, 64);
            info->sync_aids_count = (full_sync->sync_aids_count > 128) ? 128 : full_sync->sync_aids_count;
            info->riding = full_sync->riding;
            memcpy(info->sync_aids, full_sync->sync_aids, sizeof(uint32_t) * 128);
            info->sync_clock = full_sync->clock;
            info->client_sync_flag = 1;
          }
        }
        enet_packet_destroy(event.packet);
        break;
      }
      case ENET_EVENT_TYPE_CONNECT:
        if (gMultiplayerData.local_role == 1) {
          gMultiplayerData.join_status = 4;
        } else if (gMultiplayerData.local_role == 0) {
          PacketFullSync sync;
          memset(&sync, 0, sizeof(PacketFullSync));
          sync.header.type = PacketType::FULL_SYNC;
          sync.header.sequenceNum = gMultiplayerData.sequence_num;
          sync.money = info->host_money;
          sync.gems = info->host_gems;
          sync.skill = info->host_skill;
          sync.x = info->local_x;
          sync.y = info->local_y;
          sync.z = info->local_z;
          sync.host_task = info->host_task;
          sync.host_node = info->host_node;
          memcpy(sync.host_continue, info->host_continue, 32);
          memcpy(sync.task_mask, info->task_mask, 64);
          sync.sync_aids_count = (info->sync_aids_count > 128) ? 128 : info->sync_aids_count;
          sync.riding = info->riding;
          memcpy(sync.sync_aids, info->sync_aids, sizeof(uint32_t) * 128);
          sync.clock = info->sync_clock;
          MultiplayerManager::send_to_peer(event.peer, 1, &sync, sizeof(PacketFullSync), ENET_PACKET_FLAG_RELIABLE);
        }
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        if (gMultiplayerData.local_role == 1) {
          gMultiplayerData.join_status = -1;
        }
        break;
      default:
        break;
    }
  }
}

void handle_packet_send(MultiplayerInfoGOAL* info) {
  PacketPlayerState local_state;
  local_state.header.type = PacketType::STATE_UPDATE;
  local_state.header.sequenceNum = ++gMultiplayerData.sequence_num;
  local_state.netId = gMultiplayerData.local_net_id;
  local_state.x = info->local_x;
  local_state.y = info->local_y;
  local_state.z = info->local_z;
  local_state.angle = info->local_angle;
  local_state.anim = (uint16_t)info->local_anim;
  local_state.anim_frame = info->local_anim_frame;
  local_state.level_hash = info->local_level;
  local_state.riding = info->riding;
  local_state.sidekick_anim = info->sidekick_anim;
  local_state.sidekick_frame = info->sidekick_frame;
  local_state.clock = info->sync_clock;
  MultiplayerManager::broadcast(gMultiplayerData, 0, local_state, ENET_PACKET_FLAG_UNSEQUENCED);

  if (info->out_event_seq > gMultiplayerData.last_out_event_seq) {
    PacketWorldEvent out_event;
    out_event.header.type = PacketType::EVENT_WORLD;
    out_event.header.sequenceNum = info->out_event_seq;
    out_event.event_type = info->out_event_type;
    out_event.actor_id = info->out_event_aid;
    MultiplayerManager::broadcast(gMultiplayerData, 1, out_event, ENET_PACKET_FLAG_RELIABLE);
    gMultiplayerData.last_out_event_seq = info->out_event_seq;
  }

  if (gMultiplayerData.local_role == 0 && info->enemy_count > 0) {
    PacketEnemySync enemy_packet;
    enemy_packet.header.type = PacketType::ENEMY_SYNC;
    enemy_packet.header.sequenceNum = gMultiplayerData.sequence_num;
    enemy_packet.count = info->enemy_count;
    memcpy(enemy_packet.enemies, info->enemies, sizeof(MPEnemyState) * 24);
    MultiplayerManager::broadcast(gMultiplayerData, 0, enemy_packet, ENET_PACKET_FLAG_UNSEQUENCED);
  }
}

void sync_to_goal(MultiplayerInfoGOAL* info) {
  if (!gMultiplayerData.inbound_events.empty()) {
    auto& event = gMultiplayerData.inbound_events.front();
    info->in_event_type = event.event_type;
    info->in_event_aid = event.actor_id;
    info->in_event_seq++;
    gMultiplayerData.inbound_events.erase(gMultiplayerData.inbound_events.begin());
  }

  uint32_t other_net_id = (gMultiplayerData.local_role == 0) ? 1 : 0;
  if (gMultiplayerData.remote_entities.count(other_net_id)) {
    auto& remote = gMultiplayerData.remote_entities[other_net_id];
    info->riding = remote.riding;
    if (!(info->riding && info->local_role == 0 && other_net_id == 1)) {
      info->remote_x = remote.x;
      info->remote_y = remote.y;
      info->remote_z = remote.z;
      info->remote_angle = remote.angle;
      info->remote_anim = (int32_t)remote.anim;
      info->remote_anim_frame = remote.anim_frame;
      info->remote_level = remote.level_hash;
      info->remote_packet_id = remote.last_sequence_num;
      info->sidekick_anim = remote.sidekick_anim;
      info->sidekick_frame = remote.sidekick_frame;
      info->sync_clock = remote.clock;
      info->remote_id = other_net_id;
      info->remote_role = (int32_t)other_net_id;
      info->remote_status = 1;
    } else {
      info->remote_status = 1;
    }
  } else {
    info->remote_status = 0;
  }

  info->local_role = gMultiplayerData.local_role;
  info->local_packet_id = gMultiplayerData.sequence_num;
}
}  // namespace

void pc_multi_sync_data(u32 info_ptr, u32 flags) {
  using namespace jak2;
  try {
    if (!gMultiplayerData.initialized || !gMultiplayerData.host) return;
    if (info_ptr == 0 || info_ptr < 0x1000) return;

    MultiplayerInfoGOAL* info = (MultiplayerInfoGOAL*)Ptr<u8>(info_ptr).c();
    if (!info) return;

    if (flags & 0x1) {
      handle_packet_receive(info);
    }
    
    if (flags & 0x2) {
      handle_packet_send(info);
    }

    sync_to_goal(info);
  } catch (...) {
    lg::error("[Multiplayer] Exception in pc_multi_sync_data");
  }
}

void pc_multi_disconnect() {
  MultiplayerManager::disconnect(gMultiplayerData);
}

void pc_multi_setup_host() {
  MultiplayerManager::setup_host(gMultiplayerData);
}

void pc_multi_setup_client(u32 ip_ptr) {
  using namespace jak2;
  const char* ip = Ptr<String>(ip_ptr).c()->data();
  MultiplayerManager::setup_client(gMultiplayerData, ip);
}

int pc_multi_get_status() {
  return MultiplayerScanner::get_status(gMultiplayerData);
}

void pc_multi_stop_search() {
  MultiplayerScanner::stop_search(gMultiplayerData);
}

void pc_multi_start_search() {
  MultiplayerScanner::start_search(gMultiplayerData);
}

u64 pc_multi_get_command_line_arg(u32 str_ptr) {
  using namespace jak2;
  const char* arg_name = Ptr<String>(str_ptr).c()->data();
  for (int i = 1; i < g_argc; i++) {
    if (strcmp(g_argv[i], arg_name) == 0) {
      if (i + 1 < g_argc) {
        return make_string_from_c(g_argv[i+1]);
      }
      return make_string_from_c(""); // Flag exists but no value
    }
  }
  return s7.offset; // Not found
}

void init_multiplayer_pc_port() {
  using namespace jak2;
  make_function_symbol_from_c("pc-multi-setup-host", (void*)pc_multi_setup_host);
  make_function_symbol_from_c("pc-multi-setup-client", (void*)pc_multi_setup_client);
  make_function_symbol_from_c("pc-multi-get-status", (void*)pc_multi_get_status);
  make_function_symbol_from_c("pc-multi-stop-search", (void*)pc_multi_stop_search);
  make_function_symbol_from_c("pc-multi-start-search", (void*)pc_multi_start_search);
  make_function_symbol_from_c("pc-multi-sync-data", (void*)pc_multi_sync_data);
  make_function_symbol_from_c("pc-multi-disconnect", (void*)pc_multi_disconnect);
  make_function_symbol_from_c("pc-multi-get-command-line-arg", (void*)pc_multi_get_command_line_arg);
}
