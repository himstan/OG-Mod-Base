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
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include <vector>
#include <cstring>

namespace {
struct MultiplayerData {
  float local_x = 0;
  float local_y = 0;
  float local_z = 0;
  float local_angle = 0;
  int local_anim = 0;

  float remote_x = 0;
  float remote_y = 0;
  float remote_z = 0;
  float remote_angle = 0;
  int remote_anim = 0;

  int remote_id = 0;
  int player_id = 0;
  bool initialized = false;
  int socket = -1;
};

MultiplayerData gMultiplayerData;

struct Payload {
  int id;
  float x, y, z, angle;
  int anim;
};
}

void sync_network_data() {}

void pc_multi_sync_data(u32 local_data_ptr, u32 remote_pos_ptr, u32 remote_rot_ptr) {
    if (local_data_ptr == 0 || remote_pos_ptr == 0 || remote_rot_ptr == 0) return;
    if (gMultiplayerData.socket < 0) return;

    // 1. Read local data from GOAL [X, Y, Z, Angle]
    float* local = Ptr<float>(local_data_ptr).c();
    gMultiplayerData.local_x = local[0];
    gMultiplayerData.local_y = local[1];
    gMultiplayerData.local_z = local[2];
    gMultiplayerData.local_angle = local[3];

    // Read local anim from the rotation vector X component (set in GOAL)
    float* rot_in = Ptr<float>(remote_rot_ptr).c();
    gMultiplayerData.local_anim = (int)rot_in[0];

    // 2. Send local data to server
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    Payload send_payload;
    send_payload.id = gMultiplayerData.player_id;
    send_payload.x = gMultiplayerData.local_x;
    send_payload.y = gMultiplayerData.local_y;
    send_payload.z = gMultiplayerData.local_z;
    send_payload.angle = gMultiplayerData.local_angle;
    send_payload.anim = gMultiplayerData.local_anim;

    sendto(gMultiplayerData.socket, (const char*)&send_payload, sizeof(Payload), 0, (sockaddr*)&server_addr, sizeof(server_addr));

    // 3. Drain incoming packets from server
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
        }
    }

    // 4. Write remote data back to GOAL
    float* r_pos = Ptr<float>(remote_pos_ptr).c();
    r_pos[0] = gMultiplayerData.remote_x;
    r_pos[1] = gMultiplayerData.remote_y;
    r_pos[2] = gMultiplayerData.remote_z;
    r_pos[3] = (float)gMultiplayerData.remote_id; 

    float* r_rot = Ptr<float>(remote_rot_ptr).c();
    r_rot[0] = (float)gMultiplayerData.remote_anim; 
    r_rot[1] = gMultiplayerData.remote_angle;       
    r_rot[2] = 0; 
    r_rot[3] = 0; 
}

void init_multiplayer_pc_port() {
  if (gMultiplayerData.initialized) return;

  lg::info("[Multiplayer] Initializing UDP Socket...");
  gMultiplayerData.socket = open_socket(AF_INET, SOCK_DGRAM, 0);
  if (gMultiplayerData.socket >= 0) {
#ifdef _WIN32
      unsigned long mode = 1;
      ioctlsocket(gMultiplayerData.socket, FIONBIO, &mode);
#else
      int mode = 1;
      ioctl(gMultiplayerData.socket, FIONBIO, &mode);
#endif
      gMultiplayerData.initialized = true;
      gMultiplayerData.player_id = (int)time(NULL); 
      lg::info("[Multiplayer] Socket opened: {}, ID: {}", gMultiplayerData.socket, gMultiplayerData.player_id);
  }

  jak2::make_function_symbol_from_c("pc-multi-sync-data", (void*)pc_multi_sync_data);
}
