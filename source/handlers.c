#include "handlers.h"
#include "crc.h"
#include <stdio.h>

static void handle_get_version(const bcp_response_t *response);

static void handle_run_firmware(const bcp_response_t *response);

static void handle_calc_bank_crc(const bcp_response_t *response);

void handle_response(const bcp_response_t *response, bool debug) {
    uint16_t expected = bcp_response_calculate_crc16(response);
    if (expected != response->crc) {
        printf("CRC is not identical: 0x%02X (0x%02X expected)\n", response->crc, expected);
        return;
    }

    if (response->status != BCP_OK) {
        printf("An error occurred (code %d)\n", response->status);
        return;
    }

    switch (response->command) {
    case BCP_GET_VERSION: 
        handle_get_version(response);
        return;
    case BCP_RUN_FIRMWARE:
        handle_run_firmware(response);
        return;
    case BCP_CALC_BANK_CRC:
        handle_calc_bank_crc(response);
        return;
    }
}

void handle_run_firmware(const bcp_response_t *response) {
    printf("Jumped to main application successfully\n");
}

void handle_get_version(const bcp_response_t *response) {
    if (response->length != 3) {
        printf("The version is bad");
        return;
    }

    uint8_t major_version = response->data[0];
    uint8_t minor_version = response->data[1];
    uint8_t patch_version = response->data[2];

    printf("Bootloader current version: %d.%d.%d\n", major_version, minor_version, patch_version);
}

void handle_calc_bank_crc(const bcp_response_t *response) {
    if (response->length != 2) {
        printf("The CRC is bad (incorrect bytes count)\n");
        return;
    }

    uint16_t crc = (uint16_t) response->data[0] << 8 | response->data[1];
    printf("Bank CRC is 0x%02X\n", crc);
}