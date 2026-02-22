#include <string>
#include "multiplayer.h"
#include "common/cross_sockets/XSocket.h"
#include "game/kernel/common/kmachine.h"
#include "game/kernel/jak2/kscheme.h"
#include "common/log/log.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <process.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include <cstring>
#include <ctime>

namespace {

enum class PacketType : uint8_t { 
    JOIN = 0, 
    LEAVE = 1, 
    SYNC = 2 
};

#pragma pack(push, 1)
struct PacketHeader {
    PacketType type;
    uint32_t sender_id;
};

struct PacketJoin {
    PacketHeader header;
    int32_t assigned_role;
};

struct PacketLeave {
    PacketHeader header;
};

struct PacketSync {
    PacketHeader header;
    float x, y, z, angle;
    int32_t anim, role;
    uint32_t level_hash;
};
#pragma pack(pop)

struct MultiplayerInfoGOAL {
    float local_x, local_y, local_z, local_angle;   // 0, 4, 8, 12
    float remote_x, remote_y, remote_z, remote_angle; // 16, 20, 24, 28
    uint32_t remote_id;                             // 32
    int32_t remote_role;                            // 36
    int32_t remote_anim;                            // 40
    int32_t local_anim;                             // 44
    uint32_t local_level;                           // 48
    uint32_t remote_level;                          // 52
    int32_t remote_status;                          // 56
    int32_t local_role;                             // 60
};

struct MultiplayerData {
  float local_x, local_y, local_z, local_angle;
  int local_anim, local_role;
  uint32_t local_level;
  float remote_x, remote_y, remote_z, remote_angle;
  int remote_anim, remote_role;
  uint32_t remote_level;
  uint32_t remote_id = 0;
  uint32_t player_id = 0;
  int32_t remote_status = 0;
  bool initialized = false;
  int socket = -1;
};

MultiplayerData gMultiplayerData;

}

void sync_network_data() {}

void pc_multi_sync_data(u32 info_ptr) {
    try {
        if (info_ptr == 0 || info_ptr < 0x1000) return;
        if (gMultiplayerData.socket < 0) return;

        MultiplayerInfoGOAL* info = (MultiplayerInfoGOAL*)Ptr<u8>(info_ptr).c();
        if (!info) return;

        // 1. Read Local
        gMultiplayerData.local_x = info->local_x;
        gMultiplayerData.local_y = info->local_y;
        gMultiplayerData.local_z = info->local_z;
        gMultiplayerData.local_angle = info->local_angle;
        gMultiplayerData.local_anim = info->local_anim;
        gMultiplayerData.local_level = info->local_level;

        sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(8080);
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        // 2. Initial Join (One-time or periodic until role assigned)
        if (gMultiplayerData.local_role == -1) {
            PacketHeader join_header = { PacketType::JOIN, gMultiplayerData.player_id };
            sendto(gMultiplayerData.socket, (const char*)&join_header, sizeof(PacketHeader), 0, (sockaddr*)&server_addr, sizeof(server_addr));
        }

        // 3. Send SYNC (Only if we have a role!)
        if (gMultiplayerData.local_role != -1) {
            PacketSync sync_packet;
            sync_packet.header = { PacketType::SYNC, gMultiplayerData.player_id };
            sync_packet.x = gMultiplayerData.local_x;
            sync_packet.y = gMultiplayerData.local_y;
            sync_packet.z = gMultiplayerData.local_z;
            sync_packet.angle = gMultiplayerData.local_angle;
            sync_packet.anim = gMultiplayerData.local_anim;
            sync_packet.role = gMultiplayerData.local_role;
            sync_packet.level_hash = gMultiplayerData.local_level;

            sendto(gMultiplayerData.socket, (const char*)&sync_packet, sizeof(PacketSync), 0, (sockaddr*)&server_addr, sizeof(server_addr));
        }

        // 4. Recv Loop
        char buffer[512];
        sockaddr_in from_addr;
#ifdef _WIN32
        int from_len = sizeof(from_addr);
#else
        socklen_t from_len = sizeof(from_addr);
#endif

        while (true) {
            int n = recvfrom(gMultiplayerData.socket, buffer, sizeof(buffer), 0, (sockaddr*)&from_addr, &from_len);
            if (n <= 0) break; 

            if (n >= (int)sizeof(PacketHeader)) {
                PacketHeader* header = (PacketHeader*)buffer;
                
                if (header->type == PacketType::JOIN) {
                    if (n == sizeof(PacketJoin)) {
                        PacketJoin* join = (PacketJoin*)buffer;
                        if (join->header.sender_id == gMultiplayerData.player_id) {
                            gMultiplayerData.local_role = join->assigned_role;
                            lg::info("[Multiplayer] Server assigned role: {}", gMultiplayerData.local_role);
                        } else {
                            gMultiplayerData.remote_status = 1;
                            gMultiplayerData.remote_id = join->header.sender_id;
                            gMultiplayerData.remote_role = join->assigned_role;
                        }
                    }
                } 
                else if (header->type == PacketType::LEAVE) {
                    if (header->sender_id == gMultiplayerData.remote_id) {
                        gMultiplayerData.remote_status = 0;
                        gMultiplayerData.remote_id = 0;
                    }
                }
                else if (header->type == PacketType::SYNC) {
                    if (n == sizeof(PacketSync)) {
                        PacketSync* sync = (PacketSync*)buffer;
                        if (sync->header.sender_id != gMultiplayerData.player_id) {
                            gMultiplayerData.remote_x = sync->x;
                            gMultiplayerData.remote_y = sync->y;
                            gMultiplayerData.remote_z = sync->z;
                            gMultiplayerData.remote_angle = sync->angle;
                            gMultiplayerData.remote_anim = sync->anim;
                            gMultiplayerData.remote_id = sync->header.sender_id;
                            gMultiplayerData.remote_role = sync->role;
                            gMultiplayerData.remote_level = sync->level_hash;
                            gMultiplayerData.remote_status = 1;
                        }
                    }
                }
            }
        }

        // 5. Write Remote Back to GOAL
        info->remote_x = gMultiplayerData.remote_x;
        info->remote_y = gMultiplayerData.remote_y;
        info->remote_z = gMultiplayerData.remote_z;
        info->remote_angle = gMultiplayerData.remote_angle;
        info->remote_id = gMultiplayerData.remote_id;
        info->remote_role = gMultiplayerData.remote_role;
        info->remote_anim = gMultiplayerData.remote_anim;
        info->remote_level = gMultiplayerData.remote_level;
        info->remote_status = gMultiplayerData.remote_status;
        info->local_role = gMultiplayerData.local_role;

    } catch (...) {}
}

void pc_multi_disconnect() {
    if (gMultiplayerData.socket < 0) return;
    
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    PacketHeader leave_header = { PacketType::LEAVE, gMultiplayerData.player_id };
    sendto(gMultiplayerData.socket, (const char*)&leave_header, sizeof(PacketHeader), 0, (sockaddr*)&server_addr, sizeof(server_addr));
    
    lg::info("[Multiplayer] Sent disconnect packet to server.");
}

void init_multiplayer_pc_port() {
  if (gMultiplayerData.initialized) return;

  lg::info("[Multiplayer] Initializing UDP Socket (Flat Mode)...");
  gMultiplayerData.socket = open_socket(AF_INET, SOCK_DGRAM, 0);
  if (gMultiplayerData.socket >= 0) {
#ifdef _WIN32
      unsigned long mode = 1;
      ioctlsocket(gMultiplayerData.socket, FIONBIO, &mode);
      // Improved ID generation to avoid collisions on same-second starts
      gMultiplayerData.player_id = (uint32_t)_getpid() ^ ((uint32_t)time(NULL) << 8);
#else
      int mode = 1;
      ioctl(gMultiplayerData.socket, FIONBIO, &mode);
      gMultiplayerData.player_id = (uint32_t)getpid() ^ ((uint32_t)time(NULL) << 8);
#endif
      gMultiplayerData.local_role = -1; // Not yet assigned
      gMultiplayerData.initialized = true;
      lg::info("[Multiplayer] Socket opened: {}, Unique ID: {}", gMultiplayerData.socket, gMultiplayerData.player_id);
  }

  jak2::make_function_symbol_from_c("pc-multi-sync-data", (void*)pc_multi_sync_data);
}
