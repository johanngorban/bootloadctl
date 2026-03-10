#include "bcp.h"
#include "crc.h"
#include <string.h>
#include <unistd.h> 
#include <stdio.h>

uint8_t bcp_request_init(bcp_request_t *request) {
    if (request == NULL) {
        return 1;
    }
    memset(request, 0, sizeof(bcp_request_t));
    return 0;
}

uint8_t bcp_request_parse(bcp_request_t *request, const uint8_t *data, uint8_t length) {
    if (request == NULL || data == NULL) {
        return 1;
    }
    if (length < 5) {
        return 1;
    }
    if (data[0] != BCP_SOF_BYTE) {
        return 1;
    }

    request->command = data[1];
    request->length  = data[2];

    if (request->length > 0) {
        memcpy(request->data, &data[3], request->length);
    }

    request->crc = (uint16_t) (data[3 + request->length]) | (uint16_t) (data[3 + request->length + 1] << 8);

    return 0;
}

uint8_t bcp_response_init(bcp_response_t *response) {
    if (response == NULL) {
        return 1;
    }
    memset(response, 0, sizeof(bcp_response_t));
    return 0;
}

uint8_t bcp_response_set_data(bcp_response_t *response, const uint8_t *data, uint8_t length) {
    if (response == NULL || data == NULL) {
        return 1;
    }

    if (length > BCP_MAX_DATA_LENGTH) {
        return 2;
    }

    memcpy(response->data, data, length);
    response->crc = bcp_response_calculate_crc16(response);

    return 0;
}

uint8_t bcp_response_to_bytes(const bcp_response_t *response, uint8_t *data) {
    return 0;
}

inline uint16_t bcp_request_calculate_crc16(const bcp_request_t *request) {
    return crc16_calculate((const uint8_t *) request, request->length + BCP_REQUEST_HEADER_SIZE);
}

inline uint16_t bcp_response_calculate_crc16(const bcp_response_t *response) {
    return crc16_calculate((const uint8_t *) response, response->length + BCP_RESPONSE_HEADER_SIZE);
}

static int read_n(int fd, void *buf, size_t n) {
    size_t received = 0;
    while (received < n) {
        int r = read(fd, (uint8_t*)buf + received, n - received);
        if (r <= 0) {
            return -1;
        }
        received += r;
    }
    return 0;
}

void bcp_send_request(int fd, const bcp_request_t *request) {
    uint16_t packet_length = 1 + BCP_REQUEST_HEADER_SIZE + request->length + 2;
    uint8_t packet[packet_length];

    packet[0] = BCP_SOF_BYTE;
    packet[1] = request->command;
    packet[2] = request->length;
    memcpy(&packet[3], request->data, request->length);
    memcpy(&packet[3 + request->length], &request->crc, 2);

    write(fd, packet, packet_length);
}

uint8_t bcp_get_response(int fd, bcp_response_t *response) {
    uint8_t sof_byte = 0;
    uint8_t attempt = 0;

    do {
        if (attempt >= 5) {
            printf("Too many attempts. Try later\n");
            return 0;
        }
        if (read_n(fd, &sof_byte, 1) < 0) {
            printf("Read error\n");
            return 0;
        }
        attempt++;
    } while (sof_byte != BCP_SOF_BYTE);

    if (read_n(fd, &response->command, 1) < 0) {
        return 0;
    }
    if (read_n(fd, &response->status,  1) < 0) {
        return 0;
    }
    if (read_n(fd, &response->length,  1) < 0) {
        return 0;
    }

    if (response->length > BCP_MAX_DATA_LENGTH) {
        printf("Invalid length: %d\n", response->length);
        return 0;
    }

    if (read_n(fd, response->data, response->length) < 0) {
        return 0;
    }
    if (read_n(fd, &response->crc, 2) < 0) {
        return 0;
    }

    return 1;
} 