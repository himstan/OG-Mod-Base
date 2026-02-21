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

#include <vector>
#include <cstring>
#include <ctime>

namespace {
struct MultiplayerInfoGOAL {
    float local_x, local_y, local_z, local_angle;   // 0, 4, 8, 12
    float remote_x, remote_y, remote_z, remote_angle; // 16, 20, 24, 28
    uint32_t remote_id;                             // 32
    int32_t remote_role;                            // 36
    int32_t remote_anim;                            // 40
    int32_t local_anim;                             // 44
};

struct MultiplayerData {
  float local_x, local_y, local_z, local_angle;
  int local_anim, local_role;
  float remote_x, remote_y, remote_z, remote_angle;
  int remote_anim, remote_role;
  uint32_t remote_id = 0;
  uint32_t player_id = 0;
  bool initialized = false;
  int socket = -1;
};

MultiplayerData gMultiplayerData;

struct Payload {
  uint32_t id;
  float x, y, z, angle;
  int32_t anim, role;
};
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

        // 2. Assign Role (smaller ID = Jak)
        if (gMultiplayerData.remote_id != 0) {
            gMultiplayerData.local_role = (gMultiplayerData.player_id < gMultiplayerData.remote_id) ? 0 : 1;
        } else {
            gMultiplayerData.local_role = 0;
        }

        // 3. Send
        sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(8080);
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        Payload send_payload = {
            gMultiplayerData.player_id,
            gMultiplayerData.local_x, gMultiplayerData.local_y, gMultiplayerData.local_z,
            gMultiplayerData.local_angle,
            gMultiplayerData.local_anim,
            gMultiplayerData.local_role
        };
        sendto(gMultiplayerData.socket, (const char*)&send_payload, sizeof(Payload), 0, (sockaddr*)&server_addr, sizeof(server_addr));

        // 4. Recv
        Payload recv_payload;
        sockaddr_in from_addr;
#ifdef _WIN32
        int from_len = sizeof(from_addr);
#else
        socklen_t from_len = sizeof(from_addr);
#endif

        while (true) {
            int n = recvfrom(gMultiplayerData.socket, (char*)&recv_payload, sizeof(Payload), 0, (sockaddr*)&from_addr, &from_len);
            if (n <= 0) break; 

            if (n == sizeof(Payload) && recv_payload.id != gMultiplayerData.player_id) {
                gMultiplayerData.remote_x = recv_payload.x;
                gMultiplayerData.remote_y = recv_payload.y;
                gMultiplayerData.remote_z = recv_payload.z;
                gMultiplayerData.remote_angle = recv_payload.angle;
                gMultiplayerData.remote_anim = recv_payload.anim;
                gMultiplayerData.remote_id = recv_payload.id;
                gMultiplayerData.remote_role = recv_payload.role;
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

    } catch (...) {}
}

void init_multiplayer_pc_port() {
  if (gMultiplayerData.initialized) return;

  lg::info("[Multiplayer] Initializing UDP Socket (Flat Mode)...");
  gMultiplayerData.socket = open_socket(AF_INET, SOCK_DGRAM, 0);
  if (gMultiplayerData.socket >= 0) {
#ifdef _WIN32
      unsigned long mode = 1;
      ioctlsocket(gMultiplayerData.socket, FIONBIO, &mode);
      gMultiplayerData.player_id = (uint32_t)_getpid() + (uint32_t)time(NULL);
#else
      int mode = 1;
      ioctl(gMultiplayerData.socket, FIONBIO, &mode);
      gMultiplayerData.player_id = (uint32_t)getpid() + (uint32_t)time(NULL);
#endif
      gMultiplayerData.initialized = true;
      lg::info("[Multiplayer] Socket opened: {}, Unique ID: {}", gMultiplayerData.socket, gMultiplayerData.player_id);
  }

  jak2::make_function_symbol_from_c("pc-multi-sync-data", (void*)pc_multi_sync_data);
}
