#include "crc.h"
#include <stdbool.h>

/**
 * THIS CODE AI-GENERATED AND SHOULD BE REWRITTEN!
 * CRC16-Modbus
 */

#define CRC16_TABLE_LENGTH (256)

static uint16_t crc16_table[CRC16_TABLE_LENGTH]; 
static bool crc16_table_inited = false;

void crc16_table_init() {
    if (crc16_table_inited == true) {
        return;
    }
	
	for (uint16_t i = 0; i < CRC16_TABLE_LENGTH; i++) {
		uint16_t crc = 0;
		uint16_t c = i;
		for (uint16_t j = 0; j < 8; j++) {
			if ((crc ^ c) & 0x0001) {
                crc = ( crc >> 1 ) ^ CRC16_POLYNOM;
            }
			else {
                crc = crc >> 1;
            }

			c = c >> 1;
		}
		crc16_table[i] = crc;
	}

	crc16_table_inited = true;
}

static inline uint16_t crc16_calculate_fast(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;

    while (length--) {
        uint8_t index = (crc ^ *data++) & 0xFF;
        crc = (crc >> 8) ^ crc16_table[index];
    }

    return crc;
}

uint16_t crc16_calculate(const uint8_t *data, uint16_t length) {
    if (crc16_table_inited) {
        return crc16_calculate_fast(data, length);
    }

    uint16_t crc = 0xFFFF;
    while (length--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ CRC16_POLYNOM;
            }
            else {
                crc >>= 1;
            }
        }
    }

    return crc;
}