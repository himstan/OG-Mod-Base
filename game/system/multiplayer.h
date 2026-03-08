#pragma once
#include "common/common_types.h"

void init_multiplayer_pc_port();
void pc_multi_disconnect();

void pc_multi_setup_host();
void pc_multi_setup_client(u32 ip_ptr);
int pc_multi_get_status();
void pc_multi_stop_search();
