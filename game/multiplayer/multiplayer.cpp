#include "multiplayer.h"
#include "multiplayer_manager.h"
#include "multiplayer_scanner.h"
#include "multiplayer_types.h"
#include "multiplayer_protocol.h"

#include "common/log/log.h"
#include "game/kernel/common/kmachine.h"
#include "game/kernel/jak2/kscheme.h"
#include "enet/enet.h"
#include <cstring>

namespace {
MultiplayerData gMultiplayerData;
bool g_multi_debug_stop_receive = false;

void handle_packet_receive(LocalPlayerInfoGOAL* local, RemotePlayerInfoGOAL* remote) {
  ENetEvent event;
  uint32_t current_time = enet_time_get();

  while (enet_host_service(gMultiplayerData.host, &event, 0) > 0) {
    if (g_multi_debug_stop_receive) {
      if (event.type == ENET_EVENT_TYPE_RECEIVE) {
        enet_packet_destroy(event.packet);
      }
      continue;
    }

    switch (event.type) {
      case ENET_EVENT_TYPE_RECEIVE:
        gMultiplayerData.last_receive_time = current_time;
        
        // If we were reconnecting, restore the pre-reconnect status
        if (gMultiplayerData.join_status == (int)MultiplayerStatus::RECONNECTING) {
          lg::info("[Multiplayer] Data packet received. Connection restored. Resuming status {}...", gMultiplayerData.pre_reconnect_status);
          gMultiplayerData.join_status = gMultiplayerData.pre_reconnect_status;
        }

        if (event.packet->dataLength >= sizeof(PacketHeader)) {
          PacketHeader* header = (PacketHeader*)event.packet->data;

          if (header->type == PacketType::STATE_UPDATE &&
              event.packet->dataLength == sizeof(PacketPlayerState)) {
            PacketPlayerState* state = (PacketPlayerState*)event.packet->data;
            auto& entity = gMultiplayerData.remote_entities[state->netId];
            if (state->header.sequenceNum > entity.last_sequence_num) {
              entity.status = state->status;
              entity.x = state->x;
              entity.y = state->y;
              entity.z = state->z;
              entity.angle = state->angle;
              entity.anim = state->anim;
              entity.anim_frame = state->anim_frame;
              entity.last_anim_frame = state->last_anim_frame;
              entity.level_hash = state->level_hash;
              entity.riding = state->riding;
              entity.sidekick_anim = state->sidekick_anim;
              entity.sidekick_frame = state->sidekick_frame;
              entity.last_sidekick_frame = state->last_sidekick_frame;
              entity.clock = state->clock;
              entity.last_sequence_num = state->header.sequenceNum;
            }
          } else if (header->type == PacketType::EVENT_GAME &&
                     event.packet->dataLength == sizeof(PacketGameEvent)) {
            PacketGameEvent* evt = (PacketGameEvent*)event.packet->data;
            uint32_t etype = *(uint32_t*)evt->raw_data;
            lg::info("[Multiplayer] Received Game Event: Type {}", etype);
            gMultiplayerData.inbound_events.push_back(*evt);
          } else if (header->type == PacketType::ENEMY_SYNC &&
                     event.packet->dataLength == sizeof(PacketEnemySync)) {
            PacketEnemySync* enemy_sync = (PacketEnemySync*)event.packet->data;
            if (gMultiplayerData.local_role == 1) {
              local->enemy_count = enemy_sync->count;
              memcpy(local->enemies, enemy_sync->enemies, sizeof(MPEnemyState) * 24);
            }
          } else if (header->type == PacketType::FULL_SYNC &&
                     event.packet->dataLength == sizeof(PacketFullSync)) {
            PacketFullSync* full_sync = (PacketFullSync*)event.packet->data;
            local->sync_money = full_sync->money;
            local->sync_gems = full_sync->gems;
            local->sync_skill = full_sync->skill;
            
            // For full sync we snap remote to host pos
            remote->x = full_sync->x;
            remote->y = full_sync->y;
            remote->z = full_sync->z;

            local->host_task = full_sync->host_task;
            local->host_node = full_sync->host_node;
            memcpy(local->host_continue, full_sync->host_continue, 32);
            memcpy(local->task_mask, full_sync->task_mask, 64);
            local->sync_aids_count = (full_sync->sync_aids_count > 128) ? 128 : full_sync->sync_aids_count;
            local->riding = full_sync->riding;
            memcpy(local->sync_aids, full_sync->sync_aids, sizeof(uint32_t) * 128);
            local->clock = full_sync->clock;
            local->sync_flag = 1;
          }
        }
        enet_packet_destroy(event.packet);
        break;
      case ENET_EVENT_TYPE_CONNECT:
        gMultiplayerData.last_receive_time = current_time; // Start the clock on connect
        if (gMultiplayerData.local_role == 1) {
          // Client successfully connected via ENet
          lg::info("[Multiplayer] Successfully connected to host.");
          gMultiplayerData.join_status = (int)MultiplayerStatus::CONNECTED_LOBBY;
        } else if (gMultiplayerData.local_role == 0) {
          // Peer connected to host
          char ip[64];
          enet_address_get_host_ip(&event.peer->address, ip, 64);
          lg::info("[Multiplayer] Client connected from {}:{}", ip, event.peer->address.port);
          if (gMultiplayerData.join_status != (int)MultiplayerStatus::IN_GAME) {
            gMultiplayerData.join_status = (int)MultiplayerStatus::CONNECTED_LOBBY;
          } else {
            gMultiplayerData.pending_full_sync = true;
          }
        }
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        if (gMultiplayerData.local_role == 0) {
          // Peer disconnected from host
          uint32_t peer_net_id = 1; // For now we only have 1 client
          lg::info("[Multiplayer] Client disconnected. Clearing remote entity.");
          gMultiplayerData.remote_entities.erase(peer_net_id);
          // Host stays in whatever status they were in (likely IN_GAME)
        } else {
          // Client lost connection to host
          lg::warn("[Multiplayer] Disconnected from host.");
          gMultiplayerData.join_status = (int)MultiplayerStatus::FAILED;
        }
        break;
      default:
        break;
    }
  }
}

void handle_packet_send(LocalPlayerInfoGOAL* local, MPEventBufferGOAL* events) {
  PacketPlayerState local_state;
  local_state.header.type = PacketType::STATE_UPDATE;
  local_state.header.sequenceNum = ++gMultiplayerData.sequence_num;
  local_state.netId = gMultiplayerData.local_net_id;
  local_state.status = (uint8_t)gMultiplayerData.join_status;
  local_state.x = local->x;
  local_state.y = local->y;
  local_state.z = local->z;
  local_state.angle = local->angle;
  local_state.anim = (uint16_t)local->anim;
  local_state.anim_frame = local->anim_frame;
  local_state.last_anim_frame = local->last_anim_frame;
  local_state.level_hash = local->level;
  local_state.riding = local->riding;
  local_state.sidekick_anim = local->sidekick_anim;
  local_state.sidekick_frame = local->sidekick_frame;
  local_state.last_sidekick_frame = local->last_sidekick_frame;
  local_state.clock = local->clock;
  MultiplayerManager::broadcast(gMultiplayerData, 0, local_state, ENET_PACKET_FLAG_UNSEQUENCED);

  if (gMultiplayerData.local_role == 0 && gMultiplayerData.pending_full_sync) {
    gMultiplayerData.pending_full_sync = false;
    lg::info("[Multiplayer] Sending FullSync broadcast...");
    PacketFullSync sync;
    memset(&sync, 0, sizeof(PacketFullSync));
    sync.header.type = PacketType::FULL_SYNC;
    sync.header.sequenceNum = ++gMultiplayerData.sequence_num;
    sync.money = local->money;
    sync.gems = local->gems;
    sync.skill = local->skill;
    sync.x = local->x;
    sync.y = local->y;
    sync.z = local->z;
    sync.host_task = local->host_task;
    sync.host_node = local->host_node;
    memcpy(sync.host_continue, local->host_continue, 32);
    memcpy(sync.task_mask, local->task_mask, 64);
    sync.sync_aids_count = (local->sync_aids_count > 128) ? 128 : local->sync_aids_count;
    sync.riding = local->riding;
    memcpy(sync.sync_aids, local->sync_aids, sizeof(uint32_t) * 128);
    sync.clock = local->clock;
    MultiplayerManager::broadcast(gMultiplayerData, 1, sync, ENET_PACKET_FLAG_RELIABLE);
  }

  if (gMultiplayerData.local_role == 0 && local->enemy_count > 0) {
    PacketEnemySync enemy_packet;
    enemy_packet.header.type = PacketType::ENEMY_SYNC;
    enemy_packet.header.sequenceNum = gMultiplayerData.sequence_num;
    enemy_packet.count = local->enemy_count;
    memcpy(enemy_packet.enemies, local->enemies, sizeof(MPEnemyState) * 24);
    MultiplayerManager::broadcast(gMultiplayerData, 0, enemy_packet, ENET_PACKET_FLAG_UNSEQUENCED);
  }
}

void sync_to_goal(RemotePlayerInfoGOAL* remote_goal) {
  uint32_t other_net_id = (gMultiplayerData.local_role == 0) ? 1 : 0;
  if (gMultiplayerData.remote_entities.count(other_net_id)) {
    auto& remote_state = gMultiplayerData.remote_entities[other_net_id];
    
    // Fill the Goal Remote struct
    remote_goal->x = remote_state.x;
    remote_goal->y = remote_state.y;
    remote_goal->z = remote_state.z;
    remote_goal->angle = remote_state.angle;
    remote_goal->id = other_net_id;
    remote_goal->role = (int32_t)other_net_id;
    remote_goal->anim = (int32_t)remote_state.anim;
    remote_goal->anim_frame = remote_state.anim_frame;
    remote_goal->last_anim_frame = remote_state.last_anim_frame;
    remote_goal->level = remote_state.level_hash;
    // Map network status (3, 4) to GOAL status field.
    // If it's connected (>=3), GOAL sees it as active (1) or the specific state.
    remote_goal->status = (remote_state.status > 0) ? (int32_t)remote_state.status : 1;
    remote_goal->packet_id = remote_state.last_sequence_num;
    remote_goal->riding = remote_state.riding;
    remote_goal->sidekick_anim = remote_state.sidekick_anim;
    remote_goal->sidekick_frame = remote_state.sidekick_frame;
    remote_goal->last_sidekick_frame = remote_state.last_sidekick_frame;
    remote_goal->clock = remote_state.clock;

  } else {
    remote_goal->status = 0;
  }
}
}  // namespace

int pc_multi_get_role() {
  return gMultiplayerData.local_role;
}

void pc_multi_poll(u32 local_ptr, u32 remote_ptr) {
  using namespace jak2;
  try {
    if (!gMultiplayerData.initialized || !gMultiplayerData.host) return;

    // Throttle polling if not in-game to reduce overhead
    // We only throttle during initial connection/searching (IDLE, SEARCHING, CONNECTING, etc.)
    static uint32_t last_poll_tick = 0;
    uint32_t current_time = enet_time_get();
    
    bool is_in_game = (gMultiplayerData.join_status == (int)MultiplayerStatus::IN_GAME);
    bool is_reconnecting = (gMultiplayerData.join_status == (int)MultiplayerStatus::RECONNECTING);

    if (!is_in_game && !is_reconnecting) {
      if (current_time - last_poll_tick < 100) {
        return;
      }
    }
    
    last_poll_tick = current_time;

    LocalPlayerInfoGOAL* local = (LocalPlayerInfoGOAL*)Ptr<u8>(local_ptr).c();
    RemotePlayerInfoGOAL* remote = (RemotePlayerInfoGOAL*)Ptr<u8>(remote_ptr).c();
    if (!local || !remote) return;
    handle_packet_receive(local, remote);

    // Timeout Check: If we are in-game but haven't received anything in 10s, trigger Reconnecting UI
    // Host only checks if at least one peer is connected. Client always checks.
    bool should_check_timeout = (gMultiplayerData.local_role == 1) || 
                               (gMultiplayerData.local_role == 0 && gMultiplayerData.host->connectedPeers > 0);

    if (should_check_timeout &&
        gMultiplayerData.join_status == (int)MultiplayerStatus::IN_GAME &&
        gMultiplayerData.last_receive_time != 0 &&
        current_time - gMultiplayerData.last_receive_time > 10000) {
      lg::warn("[Multiplayer] Connection timed out (10s). Entering RECONNECTING state...");
      gMultiplayerData.pre_reconnect_status = gMultiplayerData.join_status;
      gMultiplayerData.join_status = (int)MultiplayerStatus::RECONNECTING;
    }
  } catch (...) {
    lg::error("[Multiplayer] Exception in pc_multi_poll");
  }
}

void pc_multi_send_state(u32 local_ptr) {
  if (g_multi_debug_stop_receive) return;
  using namespace jak2;
  try {
    if (!gMultiplayerData.initialized || !gMultiplayerData.host || local_ptr < 0x1000) return;
    LocalPlayerInfoGOAL* local = (LocalPlayerInfoGOAL*)Ptr<u8>(local_ptr).c();
    if (!local) return;
    handle_packet_send(local, nullptr);
  } catch (...) {
    lg::error("[Multiplayer] Exception in pc_multi_send_state");
  }
}

void pc_multi_receive_state(u32 remote_ptr) {
  using namespace jak2;
  try {
    if (!gMultiplayerData.initialized || !gMultiplayerData.host) return;
    if (remote_ptr < 0x1000) return;
    RemotePlayerInfoGOAL* remote = (RemotePlayerInfoGOAL*)Ptr<u8>(remote_ptr).c();
    if (!remote) return;
    sync_to_goal(remote);
  } catch (...) {
    lg::error("[Multiplayer] Exception in pc_multi_receive_state");
  }
}

void pc_multi_send_events(u32 event_ptr) {
  if (g_multi_debug_stop_receive) return;
  using namespace jak2;
  try {
    if (!gMultiplayerData.initialized || !gMultiplayerData.host || event_ptr < 0x1000) return;
    MPEventBufferGOAL* events = (MPEventBufferGOAL*)Ptr<u8>(event_ptr).c();
    if (!events) return;
    
    // Batched Outbound Events
    if (events->out_count > 0) {
      lg::info("[Multiplayer] Sending {} batched events", events->out_count);
      for (uint32_t i = 0; i < events->out_count && i < 16; ++i) {
        PacketGameEvent out_event;
        out_event.header.type = PacketType::EVENT_GAME;
        out_event.header.sequenceNum = ++gMultiplayerData.last_out_event_seq;
        // Copy the entire 48-byte GOAL mp-event structure (etype + pad + data)
        memcpy(out_event.raw_data, &events->out_events[i], 48);
        MultiplayerManager::broadcast(gMultiplayerData, 1, out_event, ENET_PACKET_FLAG_RELIABLE);
        lg::info("  [Event] Type: {}", events->out_events[i].etype);
      }
    }
    events->out_count = 0; // Clear after sending
  } catch (...) {
    lg::error("[Multiplayer] Exception in pc_multi_send_events");
  }
}

void pc_multi_receive_events(u32 event_ptr) {
  using namespace jak2;
  try {
    if (!gMultiplayerData.initialized || !gMultiplayerData.host || event_ptr < 0x1000) return;
    MPEventBufferGOAL* events = (MPEventBufferGOAL*)Ptr<u8>(event_ptr).c();
    if (!events) return;

    // Batched Inbound Events - APPEND only, do not clear (GOAL clears at end of frame)
    while (!gMultiplayerData.inbound_events.empty() && events->in_count < 16) {
      auto& event = gMultiplayerData.inbound_events.front();
      // Copy the entire 48-byte structure back to GOAL
      memcpy(&events->in_events[events->in_count], event.raw_data, 48);
      events->in_count++;
      gMultiplayerData.inbound_events.erase(gMultiplayerData.inbound_events.begin());
    }
  } catch (...) {
    lg::error("[Multiplayer] Exception in pc_multi_receive_events");
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

void pc_multi_set_status(int status) {
  int old_status = gMultiplayerData.join_status;
  gMultiplayerData.join_status = status;
  
  if (old_status != status) {
    lg::info("[Multiplayer] Status transition: {} -> {}", old_status, status);
  }

  // If Host is transitioning to IN_GAME, broadcast a FullSync to all connected clients
  // This allows clients to start their loading process with the correct world state.
  // We only do this on the transition from a non-game state to IN_GAME to avoid
  // triggering hard-reloads on clients every time the host respawns.
  if (gMultiplayerData.local_role == 0 && 
      status == (int)MultiplayerStatus::IN_GAME && 
      old_status != (int)MultiplayerStatus::IN_GAME) {
    lg::info("[Multiplayer] Host entered game. Broadcasting FullSync to peers...");
    gMultiplayerData.pending_full_sync = true;
  }
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

u64 pc_multi_get_found_ip() {
  using namespace jak2;
  return make_string_from_c(gMultiplayerData.found_ip.c_str());
}

void pc_multi_debug_stop_receive(u32 val) {
  g_multi_debug_stop_receive = (val != 0);
}

void init_multiplayer_pc_port() {
  using namespace jak2;
  make_function_symbol_from_c("pc-multi-setup-host", (void*)pc_multi_setup_host);
  make_function_symbol_from_c("pc-multi-setup-client", (void*)pc_multi_setup_client);
  make_function_symbol_from_c("pc-multi-get-status", (void*)pc_multi_get_status);
  make_function_symbol_from_c("pc-multi-set-status", (void*)pc_multi_set_status);
  make_function_symbol_from_c("pc-multi-stop-search", (void*)pc_multi_stop_search);
  make_function_symbol_from_c("pc-multi-start-search", (void*)pc_multi_start_search);
  make_function_symbol_from_c("pc-multi-get-found-ip", (void*)pc_multi_get_found_ip);
  make_function_symbol_from_c("pc-multi-poll", (void*)pc_multi_poll);
  make_function_symbol_from_c("pc-multi-send-state", (void*)pc_multi_send_state);
  make_function_symbol_from_c("pc-multi-receive-state", (void*)pc_multi_receive_state);
  make_function_symbol_from_c("pc-multi-send-events", (void*)pc_multi_send_events);
  make_function_symbol_from_c("pc-multi-receive-events", (void*)pc_multi_receive_events);
  make_function_symbol_from_c("pc-multi-get-role", (void*)pc_multi_get_role);
  make_function_symbol_from_c("pc-multi-disconnect", (void*)pc_multi_disconnect);
  make_function_symbol_from_c("pc-multi-get-command-line-arg", (void*)pc_multi_get_command_line_arg);
  make_function_symbol_from_c("pc-multi-debug-stop-receive", (void*)pc_multi_debug_stop_receive);
}
