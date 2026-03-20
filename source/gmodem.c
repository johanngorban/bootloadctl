#include "gmodem.h"
#include "crc.h"
#include "io.h"
#include <string.h>

#define GMODEM_SOH  (0x01)
#define GMODEM_EOT  (0x04)
#define GMODEM_ACK  (0x06)
#define GMODEM_NAK  (0x15)
#define GMODEM_HEADER_SIZE (3)
#define GMODEM_FOOTER_SIZE (2)
#define GMODEM_PACKET_SIZE (1024)

void gmodem_send(int fd, const uint8_t *bytes, uint32_t length) {
    uint8_t full_blocks = length / GMODEM_PACKET_SIZE;

    uint8_t soh = GMODEM_SOH;
    uint16_t data_length = GMODEM_PACKET_SIZE;
    uint8_t *data = bytes;
    uint8_t block = 1;
    while (block <= full_blocks) {
        uint16_t crc = crc16_calculate(data, data_length);

        write_n(fd, &soh, 1);
        write_n(fd, &block, 1);
        write_n(fd, (uint8_t *) &data_length, 2);
        write_n(fd, data, data_length);
        write_n(fd, (uint8_t *) &crc, 2);

        uint8_t ack = GMODEM_NAK;
        read_n(fd, &ack, 1);
        if (ack == GMODEM_ACK) {
            data += GMODEM_PACKET_SIZE;
            block++;
        }
    }

    data_length = length % GMODEM_PACKET_SIZE;
    if (data_length > 0) {
        uint16_t crc = crc16_calculate(data, data_length);

        write_n(fd, &soh, 1);
        write_n(fd, &block, 1);
        write_n(fd, (uint8_t *) &data_length, 1);
        write_n(fd, data, data_length);
        write_n(fd, (uint8_t *) &crc, 2);

        uint8_t ack = GMODEM_NAK;
        read_n(fd, &ack, 1);
        if (ack == GMODEM_ACK) {
            data += GMODEM_PACKET_SIZE;
            block++;
        }
    }
}
