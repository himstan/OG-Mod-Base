#include <string>
#include <unordered_map>
#include <vector>
#include <enet/enet.h>

#include "multiplayer.h"
#include "game/kernel/common/kmachine.h"
#include "game/kernel/jak2/kscheme.h"
#include "common/log/log.h"

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <cstring>
#include <ctime>

namespace {

enum class PacketType : uint8_t { 
    STATE_UPDATE = 0, 
    EVENT_JOIN = 1,
    EVENT_LEAVE = 2,
    EVENT_WORLD = 3,
    FULL_SYNC = 4
};

#pragma pack(push, 1)

struct PacketHeader {
    PacketType type;
    uint32_t sequenceNum; // Incremented every tick by the sender
};

struct PacketPlayerState {
    PacketHeader header;
    uint32_t netId;
    float x, y, z, angle;
    uint16_t anim;
    float anim_frame;
    uint32_t level_hash;
};

struct PacketWorldEvent {
  PacketHeader header;
  uint32_t event_type;
  uint32_t actor_id;
};

struct PacketFullSync {
    PacketHeader header;
    float money;
    float gems;
    float skill;
    float x, y, z;
    uint32_t host_task;
    uint32_t host_node;
    uint32_t host_levels[6];
    uint8_t task_mask[64];
    uint32_t sync_aids_count;
    uint32_t sync_aids[488];
};

#pragma pack(pop)

struct RemoteEntityState {
    float x, y, z, angle;
    uint16_t anim;
    float anim_frame;
    uint32_t level_hash;
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
    uint32_t host_levels[6];
    uint8_t task_mask[64];
    uint32_t sync_aids_count;
    uint32_t sync_aids[488];
};

struct MultiplayerData {
  bool initialized = false;
  ENetHost* host = nullptr;
  ENetPeer* server_peer = nullptr; // Only used if we are a client
  int local_role = -1;
  uint32_t local_net_id = 0;
  uint32_t sequence_num = 0;
  uint32_t last_out_event_seq = 0;
  
  std::unordered_map<uint32_t, RemoteEntityState> remote_entities;
  std::vector<PacketWorldEvent> inbound_events;
};

MultiplayerData gMultiplayerData;

}

void pc_multi_sync_data(u32 info_ptr) {
    try {
        if (!gMultiplayerData.initialized || !gMultiplayerData.host) return;
        if (info_ptr == 0 || info_ptr < 0x1000) return;

        MultiplayerInfoGOAL* info = (MultiplayerInfoGOAL*)Ptr<u8>(info_ptr).c();
        if (!info) return;

        // --- 1. Network Receive (ENet Service) ---
        ENetEvent event;
        while (enet_host_service(gMultiplayerData.host, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_RECEIVE: {
                    if (event.packet->dataLength >= sizeof(PacketHeader)) {
                        PacketHeader* header = (PacketHeader*)event.packet->data;
                        
                        if (header->type == PacketType::STATE_UPDATE && event.packet->dataLength == sizeof(PacketPlayerState)) {
                            PacketPlayerState* state = (PacketPlayerState*)event.packet->data;
                            
                            // Update remote entities map
                            auto& entity = gMultiplayerData.remote_entities[state->netId];
                            if (state->header.sequenceNum > entity.last_sequence_num) {
                                entity.x = state->x;
                                entity.y = state->y;
                                entity.z = state->z;
                                entity.angle = state->angle;
                                entity.anim = state->anim;
                                entity.anim_frame = state->anim_frame;
                                entity.level_hash = state->level_hash;
                                entity.last_sequence_num = state->header.sequenceNum;
                            }
                        } else if (header->type == PacketType::EVENT_WORLD && event.packet->dataLength == sizeof(PacketWorldEvent)) {
                          PacketWorldEvent* world_event = (PacketWorldEvent*)event.packet->data;
                          lg::info("[Multiplayer] Received World Event: Type {}, AID {}", world_event->event_type, world_event->actor_id);
                          gMultiplayerData.inbound_events.push_back(*world_event);
                        } else if (header->type == PacketType::FULL_SYNC && event.packet->dataLength == sizeof(PacketFullSync)) {
                          PacketFullSync* full_sync = (PacketFullSync*)event.packet->data;
                          lg::info("[Multiplayer] Received Full Sync from Host: Money {}, Gems {}, Skill {}, Aids {}", 
                                   full_sync->money, full_sync->gems, full_sync->skill, full_sync->sync_aids_count);
                          info->client_sync_money = full_sync->money;
                          info->client_sync_gems = full_sync->gems;
                          info->client_sync_skill = full_sync->skill;
                          info->remote_x = full_sync->x;
                          info->remote_y = full_sync->y;
                          info->remote_z = full_sync->z;
                          info->host_task = full_sync->host_task;
                          info->host_node = full_sync->host_node;
                          memcpy(info->host_levels, full_sync->host_levels, sizeof(uint32_t) * 6);
                          memcpy(info->task_mask, full_sync->task_mask, 64);
                          
                          // Safety check: Clamp count to new buffer size
                          uint32_t count = full_sync->sync_aids_count;
                          if (count > 488) count = 488;
                          
                          info->sync_aids_count = count;
                          memcpy(info->sync_aids, full_sync->sync_aids, sizeof(uint32_t) * 488);
                          info->client_sync_flag = 1;
                        }
                    }
                    enet_packet_destroy(event.packet);
                    break;
                }
                case ENET_EVENT_TYPE_CONNECT:
                    lg::info("[Multiplayer] Peer connected: {}:{}", event.peer->address.host, event.peer->address.port);
                    // Host Handshake: Send current save state to the new client
                    if (gMultiplayerData.local_role == 0) {
                        PacketFullSync sync;
                        memset(&sync, 0, sizeof(PacketFullSync)); // Clear to be safe
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
                        memcpy(sync.host_levels, info->host_levels, sizeof(uint32_t) * 6);
                        memcpy(sync.task_mask, info->task_mask, 64);
                        
                        uint32_t count = info->sync_aids_count;
                        if (count > 488) count = 488;
                        sync.sync_aids_count = count;
                        
                        memcpy(sync.sync_aids, info->sync_aids, sizeof(uint32_t) * 488);

                        ENetPacket* sync_packet = enet_packet_create(&sync, sizeof(PacketFullSync), ENET_PACKET_FLAG_RELIABLE);
                        enet_peer_send(event.peer, 1, sync_packet);
                        lg::info("[Multiplayer] Sent Full Sync to new peer: Money {}, Gems {}, Skill {}, Aids {}", 
                                 sync.money, sync.gems, sync.skill, sync.sync_aids_count);
                    }
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    lg::info("[Multiplayer] Peer disconnected.");
                    // In a real scenario, we might want to remove the entity associated with this peer
                    break;
                default:
                    break;
            }
        }

        // --- 2. Network Send (Broadcast/Target) ---
        // 2a. State Update (Channel 0, Unsequenced)
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

        ENetPacket* packet = enet_packet_create(&local_state, sizeof(PacketPlayerState), ENET_PACKET_FLAG_UNSEQUENCED);
        
        if (gMultiplayerData.local_role == 0) {
            enet_host_broadcast(gMultiplayerData.host, 0, packet);
        } else if (gMultiplayerData.server_peer) {
            enet_peer_send(gMultiplayerData.server_peer, 0, packet);
        }

        // 2b. World Events (Channel 1, Reliable)
        if (info->out_event_seq > gMultiplayerData.last_out_event_seq) {
          lg::info("[Multiplayer] Sending World Event: Type {}, AID {}, Seq {}", info->out_event_type, info->out_event_aid, info->out_event_seq);
          PacketWorldEvent out_event;
          out_event.header.type = PacketType::EVENT_WORLD;
          out_event.header.sequenceNum = info->out_event_seq;
          out_event.event_type = info->out_event_type;
          out_event.actor_id = info->out_event_aid;

          ENetPacket* event_packet = enet_packet_create(&out_event, sizeof(PacketWorldEvent), ENET_PACKET_FLAG_RELIABLE);
          if (gMultiplayerData.local_role == 0) {
            enet_host_broadcast(gMultiplayerData.host, 1, event_packet);
          } else if (gMultiplayerData.server_peer) {
            enet_peer_send(gMultiplayerData.server_peer, 1, event_packet);
          }
          gMultiplayerData.last_out_event_seq = info->out_event_seq;
        }

        // --- 3. GOAL Sync (Mapping remote_entities back to legacy pointer) ---
        // 3a. Update Inbound Events (Inbox)
        if (!gMultiplayerData.inbound_events.empty()) {
          auto& event = gMultiplayerData.inbound_events.front();
          info->in_event_type = event.event_type;
          info->in_event_aid = event.actor_id;
          info->in_event_seq++;
          gMultiplayerData.inbound_events.erase(gMultiplayerData.inbound_events.begin());
        }

        // 3b. Remote Player State Sync
        // For the MVP, we assume there's only one remote player (the other one)
        uint32_t other_net_id = (gMultiplayerData.local_role == 0) ? 1 : 0;
        
        if (gMultiplayerData.remote_entities.count(other_net_id)) {
            auto& remote = gMultiplayerData.remote_entities[other_net_id];
            info->remote_x = remote.x;
            info->remote_y = remote.y;
            info->remote_z = remote.z;
            info->remote_angle = remote.angle;
            info->remote_anim = (int32_t)remote.anim;
            info->remote_anim_frame = remote.anim_frame;
            info->remote_level = remote.level_hash;
            info->remote_packet_id = remote.last_sequence_num;
            info->remote_id = other_net_id;
            info->remote_role = (int32_t)other_net_id; // NetID 1 = Daxter, NetID 0 = Jak
            info->remote_status = 1;
        } else {
            info->remote_status = 0;
        }
        
        info->local_role = gMultiplayerData.local_role;
        info->local_packet_id = gMultiplayerData.sequence_num;

    } catch (...) {
        lg::error("[Multiplayer] Exception in pc_multi_sync_data");
    }
}

void pc_multi_disconnect() {
    if (!gMultiplayerData.initialized) return;

    if (gMultiplayerData.host) {
        if (gMultiplayerData.local_role == 1 && gMultiplayerData.server_peer) {
            enet_peer_disconnect_now(gMultiplayerData.server_peer, 0);
        }
        enet_host_destroy(gMultiplayerData.host);
        gMultiplayerData.host = nullptr;
    }
    
    enet_deinitialize();
    gMultiplayerData.initialized = false;
    lg::info("[Multiplayer] Disconnected and ENet deinitialized.");
}

void init_multiplayer_pc_port() {
    if (gMultiplayerData.initialized) return;

    if (enet_initialize() != 0) {
        lg::error("[Multiplayer] Error initializing ENet.");
        return;
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = 3000;

    // Try to host
    gMultiplayerData.host = enet_host_create(&address, 32, 2, 0, 0);

    if (gMultiplayerData.host) {
        lg::info("[Multiplayer] Listen server started on port 3000.");
        gMultiplayerData.local_role = 0;
        gMultiplayerData.local_net_id = 0;
    } else {
        // Fallback to client
        gMultiplayerData.host = enet_host_create(NULL, 1, 2, 0, 0);
        if (gMultiplayerData.host) {
            ENetAddress server_address;
            enet_address_set_host(&server_address, "127.0.0.1");
            server_address.port = 3000;

            gMultiplayerData.server_peer = enet_host_connect(gMultiplayerData.host, &server_address, 2, 0);
            if (gMultiplayerData.server_peer) {
                lg::info("[Multiplayer] Client connecting to 127.0.0.1:3000...");
                gMultiplayerData.local_role = 1;
                gMultiplayerData.local_net_id = 1;
            } else {
                lg::error("[Multiplayer] Could not connect to host.");
                enet_host_destroy(gMultiplayerData.host);
                gMultiplayerData.host = nullptr;
                return;
            }
        } else {
            lg::error("[Multiplayer] Could not create ENet client host.");
            return;
        }
    }

    gMultiplayerData.initialized = true;
    jak2::make_function_symbol_from_c("pc-multi-sync-data", (void*)pc_multi_sync_data);
}

void sync_network_data() {
    // Legacy function, can be empty or used for background polling if needed
}
