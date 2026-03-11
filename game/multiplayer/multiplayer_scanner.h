#pragma once

#include "multiplayer_types.h"
#include <thread>

class MultiplayerScanner {
 public:
  static void start_search(MultiplayerData& data);
  static void stop_search(MultiplayerData& data);
  static int get_status(const MultiplayerData& data);

 private:
  static void scan_thread_func(MultiplayerData* data);
};
