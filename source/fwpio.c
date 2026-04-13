#include "fwpio.h"
#include "fwp.h"
#include "crc.h"
#include "io.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

static int wait_ack(int fd, uint8_t *byte) {
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int rc = poll(&pfd, 1, FWP_TIMEOUT_MS);
    if (rc <= 0) {
        return -1;
    }
    if (read(fd, byte, 1) != 1) {
        return -1;
    }
    return 0;
}

static fwp_status_t send_packet(int fd, uint8_t type, uint16_t seq, const uint8_t *payload, uint16_t length) {
    uint16_t packet_length = 1 + FWP_HEADER_SIZE + length + 2;
    uint8_t packet[packet_length];

    packet[0] = FWP_SOF;
    packet[1] = type;
    packet[2] = seq & 0xFF;
    packet[3] = (seq >> 8) & 0xFF;
    packet[4] = length & 0xFF;
    packet[5] = (length >> 8) & 0xFF;
    if (length > 0) {
        memcpy(&packet[6], payload, length);
    }

    uint16_t crc = crc16_calculate(&packet[1], FWP_HEADER_SIZE + length);
    packet[6 + length] = crc & 0xFF;
    packet[7 + length] = (crc >> 8) & 0xFF;

    for (uint8_t attempt = 0; attempt < FWP_MAX_RETRIES; attempt++) {
        if (write_n(fd, packet, packet_length) < 0) {
            return FWP_ERR_PROTOCOL;
        }

        uint8_t response = 0;
        if (wait_ack(fd, &response) < 0) {
            continue;
        }
        if (response == FWP_ACK) {
            return FWP_OK;
        }
    }

    return FWP_ERR_MAX_RETRIES;
}

fwp_status_t fwp_transmit(int fd, const uint8_t *data, uint32_t length) {
    if (data == NULL || length == 0) {
        return FWP_ERR_PARAM;
    }

    uint16_t seq = 0;

    uint8_t size_buf[4];
    size_buf[0] = length & 0xFF;
    size_buf[1] = (length >> 8) & 0xFF;
    size_buf[2] = (length >> 16) & 0xFF;
    size_buf[3] = (length >> 24) & 0xFF;

    fwp_status_t st = send_packet(fd, FWP_TYPE_START, seq, size_buf, 4);
    if (st != FWP_OK) {
        return st;
    }
    seq++;

    uint32_t offset = 0;
    while (offset < length) {
        uint16_t chunk = (length - offset > FWP_DATA_SIZE) ? FWP_DATA_SIZE : (uint16_t) (length - offset);

        st = send_packet(fd, FWP_TYPE_DATA, seq, &data[offset], chunk);
        if (st != FWP_OK) {
            return st;
        }

        offset += chunk;
        seq++;

        printf("\rProgress: %u / %u bytes", offset, length);
        fflush(stdout);
    }
    printf("\n");

    st = send_packet(fd, FWP_TYPE_END, seq, NULL, 0);
    if (st != FWP_OK) {
        return st;
    }

    return FWP_OK;
}
