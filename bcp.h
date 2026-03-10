#pragma once

#include <stdint.h>

#define BCP_MAX_DATA_LENGTH        (255u)
#define BCP_REQUEST_HEADER_SIZE    (2u)
#define BCP_RESPONSE_HEADER_SIZE   (3u)

#define BCP_SOF_BYTE (0xAAu)

typedef enum {
    BCP_UNKNOWN_COMMAND  = 0x00,
    BCP_UPLOAD_FIRMWARE  = 0x01,
    BCP_UPDATE_FIRMWARE  = 0x02,
    BCP_CALC_BANK_CRC    = 0x03,
    BCP_RUN_FIRMWARE     = 0x04,
    BCP_GET_VERSION      = 0x05,
} bcp_command_t;

typedef enum {
    BCP_OK                    = 0x00,
    BCP_ERROR_UNKNOWN_COMMAND = 0x01,
    BCP_ERROR_INVALID_PARAM   = 0x02,
    BCP_ERROR_BAD_CRC         = 0x03,
    BCP_ERROR_BUSY            = 0x04,
    BCP_ERROR_MEMORY          = 0x05,
} bcp_status_t;

typedef struct {
    uint8_t command;
    uint8_t length;
    uint8_t data[BCP_MAX_DATA_LENGTH];
    uint16_t crc;
} bcp_request_t;

typedef struct {
    uint8_t command;
    uint8_t status;
    uint8_t length;
    uint8_t data[BCP_MAX_DATA_LENGTH];
    uint16_t crc;
} bcp_response_t;

uint8_t bcp_request_init(bcp_request_t *request);

uint8_t bcp_request_parse(bcp_request_t *request, const uint8_t *data, uint8_t length);

uint8_t bcp_response_init(bcp_response_t *response);

uint8_t bcp_response_set_data(bcp_response_t *response, const uint8_t *data, uint8_t length);

uint8_t bcp_response_to_bytes(const bcp_response_t *response, uint8_t *data);

uint16_t bcp_request_calculate_crc16(const bcp_request_t *request);

uint16_t bcp_response_calculate_crc16(const bcp_response_t *response);

void bcp_send_request(int fd, const bcp_request_t *request);

uint8_t bcp_get_response(int fd, bcp_response_t *response);
