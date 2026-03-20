#include "ymodem.h"
#include "crc.h"
#include "io.h"
#include <string.h>

#define YMODEM_SOH  (0x01)
#define YMODEM_STX  (0x02)
#define YMODEM_EOT  (0x04)
#define YMODEM_ACK  (0x06)
#define YMODEM_NAK  (0x15)
#define YMODEM_ETB  (0x17)
#define YMODEM_CAN  (0x18)
#define YMODEM_HEADER_SIZE (3)
#define YMODEM_FOOTER_SIZE (2)
#define YMODEM_PACKET_SIZE      (128)
#define YMODEM_PACKET_1K_SIZE   (1024)

static void ymodem_send_length(int fd, uint16_t length);

void ymodem_send(int fd, const uint8_t *bytes, uint16_t length) {
    ymodem_send_length(fd, length);

    uint16_t blocks = (length / YMODEM_PACKET_1K_SIZE);

    uint16_t packet_length = YMODEM_HEADER_SIZE + YMODEM_PACKET_1K_SIZE + YMODEM_FOOTER_SIZE;
    uint8_t packet[packet_length];
    memset(packet, 0, packet_length);

    packet[0] = YMODEM_STX;
    for (uint16_t i = 0; i < blocks; i++) {
        packet[1] = i;
        packet[2] = ~i;

        uint8_t *source = bytes + (YMODEM_PACKET_1K_SIZE * i);
        memcpy(packet + YMODEM_HEADER_SIZE, source, YMODEM_PACKET_1K_SIZE);

        uint16_t packet_crc = crc16_calculate(packet, YMODEM_HEADER_SIZE + YMODEM_PACKET_1K_SIZE);
        packet[YMODEM_HEADER_SIZE + YMODEM_PACKET_1K_SIZE] = (uint8_t) (packet_crc & 0xF0);
        packet[YMODEM_HEADER_SIZE + YMODEM_PACKET_1K_SIZE + 1] = (uint8_t) (packet_crc & 0x0F);

        uint8_t ack = YMODEM_NAK;
        uint8_t attempts = 0;
        do {
            if (attempts > 3) {
                return;
            }
            attempts++;
            write_n(fd, packet, packet_length);
            read_n(fd, &ack, 1);
        } while (ack != YMODEM_ACK);
    }

    uint16_t remained_bytes = length - (length * YMODEM_PACKET_1K_SIZE);
    if (remained_bytes > 0) {
        packet[1]++;
        packet[2] = ~packet[1];

        uint8_t *source = bytes + length - remained_bytes;
        memcpy(packet + YMODEM_HEADER_SIZE, source, remained_bytes);

        uint16_t packet_crc = crc16_calculate(packet, YMODEM_HEADER_SIZE + YMODEM_PACKET_1K_SIZE);
        packet[YMODEM_HEADER_SIZE + YMODEM_PACKET_1K_SIZE] = (uint8_t) (packet_crc & 0xF0);
        packet[YMODEM_HEADER_SIZE + YMODEM_PACKET_1K_SIZE + 1] = (uint8_t) (packet_crc & 0x0F);

        uint8_t ack = YMODEM_NAK;
        uint8_t attempts = 0;
        do {
            if (attempts > 3) {
                return;
            }
            attempts++;
            write_n(fd, packet, packet_length);
            read_n(fd, &ack, 1);
        } while (ack != YMODEM_ACK);
    }
}

void ymodem_send_length(int fd, uint16_t length) {
    uint16_t packet_length = YMODEM_HEADER_SIZE + YMODEM_PACKET_SIZE + YMODEM_FOOTER_SIZE;
    uint8_t packet[packet_length];
    memset(packet, 0, packet_length);

    packet[0] = YMODEM_SOH;
    packet[1] = 0x00;
    packet[2] = 0xFF;

    packet[3] = (uint8_t) (length & 0xF0);
    packet[4] = (uint8_t) (length & 0x0F);

    uint16_t packet_crc = crc16_calculate(packet, YMODEM_HEADER_SIZE + YMODEM_PACKET_SIZE);
    packet[YMODEM_HEADER_SIZE + YMODEM_PACKET_SIZE] = (uint8_t) (packet_crc & 0xF0);
    packet[YMODEM_HEADER_SIZE + YMODEM_PACKET_SIZE + 1] = (uint8_t) (packet_crc & 0x0F);

    uint8_t ack = YMODEM_NAK;
    do {
        write_n(fd, packet, packet_length);
        read_n(fd, &ack, 1);
    } while (ack != YMODEM_ACK);
}
