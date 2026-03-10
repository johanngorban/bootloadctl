#pragma once

#include <stdint.h>

#define CRC16_POLYNOM (0xA001)

void crc16_table_init();

uint16_t crc16_calculate(const uint8_t *data, uint16_t length);