#include "handlers.h"
#include "crc.h"
#include "ymodem.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

static void handle_get_version(const bcp_response_t *response);

static void handle_run_firmware(const bcp_response_t *response);

static void handle_calc_bank_crc(const bcp_response_t *response);

static void handle_upload_firmware(const context_t *context, const bcp_response_t *response);

void handle_response(const context_t *context, const bcp_response_t *response, bool debug){
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
    case BCP_UPLOAD_FIRMWARE:
        handle_upload_firmware(context, response);
        return;
    }
}

void handle_upload_firmware(const context_t *context, const bcp_response_t *response) {
    FILE *f = fopen(context->firmware_path, "rb");
    if (f == NULL) {
        printf("Cannot open the firmware file.\n");
        return;
    }

    uint8_t *buf = NULL;
    long size = 0;

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = malloc(size);
    if (buf == NULL) {
        printf("Cannot allocate memory for bin file.\n");
        return;
    }

    size_t read = fread(buf, 1, size, f);
    fclose(f);

    if (read != (size_t) size) {
        printf("Cannot read whole bin file.\n");
        free(buf);
        return;
    }

    ymodem_send(context->serial_fd, buf, size);
    free(buf);
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
