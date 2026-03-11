#include "multiplayer_scanner.h"
#include "enet/enet.h"
#include <string>
#include <vector>

void MultiplayerScanner::start_search(MultiplayerData& data) {
  if (!data.enet_initialized) {
    if (enet_initialize() != 0)
      return;
    data.enet_initialized = true;
  }
  std::thread(scan_thread_func, &data).detach();
}

void MultiplayerScanner::stop_search(MultiplayerData& data) {
  data.stop_search = true;
  data.join_status = 0;
}

int MultiplayerScanner::get_status(const MultiplayerData& data) {
  return data.join_status;
}

void MultiplayerScanner::scan_thread_func(MultiplayerData* data) {
  data->join_status = 1;  // Searching
  data->stop_search = false;

  std::vector<std::string> ips_to_check = {"127.0.0.1"};
  // Basic local network scan
  for (int i = 1; i < 255; ++i) {
    for (int j = 1; j < 255; ++j) {
      ips_to_check.push_back("192.168." + std::to_string(i) + "." + std::to_string(j));
    }
  }

  for (const auto& ip : ips_to_check) {
    if (data->stop_search)
      break;

    ENetAddress address;
    enet_address_set_host(&address, ip.c_str());
    address.port = 3000;

    if (ip == "127.0.0.1") {
      data->found_ip = ip;
      data->join_status = 2;  // Found
      return;
    }
  }

  if (data->join_status == 1) {
    data->join_status = -1;  // Not found
  }
}
