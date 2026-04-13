#pragma once

#include <stdint.h>

#define FWP_SOF             (0xAAu)

#define FWP_TYPE_START      (0x01u)
#define FWP_TYPE_DATA       (0x02u)
#define FWP_TYPE_END        (0x03u)

#define FWP_ACK             (0x06u)
#define FWP_NAK             (0x15u)

#define FWP_DATA_SIZE       (256u)
#define FWP_HEADER_SIZE     (5u)
#define FWP_MAX_RETRIES     (10u)
#define FWP_TIMEOUT_MS      (3000u)

typedef enum {
    FWP_OK = 0,
    FWP_ERR_PARAM,
    FWP_ERR_TIMEOUT,
    FWP_ERR_CRC,
    FWP_ERR_SEQUENCE,
    FWP_ERR_PROTOCOL,
    FWP_ERR_MAX_RETRIES,
    FWP_ERR_FLASH,
} fwp_status_t;
