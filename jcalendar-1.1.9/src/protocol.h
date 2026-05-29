#pragma once

#include <Arduino.h>
#include <stdint.h>

void init_protocol_state();
void process_command_packet(const uint8_t* data, uint16_t len);
void process_deferred_job();
bool has_pending_responses();
void refresh_msd_payload();
String get_chip_id_hex();
