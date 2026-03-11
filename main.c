#include "bcp.h"
#include <termios.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>

int serial_port_init(int fd, int speed, int parity, bool blocking) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        printf("Error %d from tcgetattr\n", errno);
        return -1;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK; // Disable break processing
    tty.c_lflag = 0;        // No signaling char, no echo
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = (int) blocking;     // Read doesn't block
    tty.c_cc[VTIME] = 10;    // 1 seconds read timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);

    tty.c_cflag |= parity;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error %d from tcsetattr\n", errno);
        return -1;
    }

    return 0;
}

void bcp_response_print(const bcp_response_t *r) {
    printf("┌─ BCP Response ───────────────────────┐\n");
    printf("│ command  : 0x%02X                       │\n", r->command);
    printf("│ status   : 0x%02X                       │\n", r->status);
    printf("│ length   : 0x%02X (%d)                  │\n", r->length, r->length);
    printf("│ crc      : 0x%04X                     │\n", r->crc);
    printf("│ data     :                            │\n");

    for (uint8_t i = 0; i < r->length; i++) {
        if (i % 8 == 0) printf("│   %04X: ", i);
        printf("%02X ", r->data[i]);
        if ((i + 1) % 8 == 0 || i == r->length - 1) {
            int pad = 8 - ((i % 8) + 1);
            for (int p = 0; p < pad; p++) printf("   ");
            printf("│\n");
        }
    }

    printf("└───────────────────────────────────────┘\n");
}

// bootctl <command> [command args] [port <PORT>]
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: bootctl <command> [command args] [port <PORT>]\n");
        return 1;
    }

    bcp_request_t request;
    bcp_request_init(&request);

    char *command = argv[1];
    argc -= 2;
    argv += 2;
    if (strcmp(command, "upload") == 0) {
        if (argc < 2 || (strcmp(argv[1], "--protocol") == 0)) {
            printf("crc requires argument \"--protocol\"\n");
            return 1;
        }
        request.command = BCP_UPLOAD_FIRMWARE;

        char *protocol = argv[2];
        if (strcmp(protocol, "xmodem") == 0) {
            request.data[0] = 1; // mock!!!
            request.length = 1;
        }
        else {
            printf("Unknown protocol for upload: %s\n", protocol);
            return 1;
        }
        argc -= 2;
        argv += 2;
    } 
    else if (strcmp(command, "update") == 0) {
        if (argc != 0) {
            printf("Unknown arguments for update\n");
            return 1;
        }
        request.command = BCP_UPDATE_FIRMWARE;
    }
    else if (strcmp(command, "crc") == 0) {
        if (argc < 2 || (strcmp(argv[1], "--bank") == 0)) {
            printf("crc requires argument \"bank\"\n");
            return 1;
        }
        request.command = BCP_CALC_BANK_CRC;
        request.data[0] = atoi(argv[0]);
        request.length = 1;

        argc -= 2;
        argv += 2;
    } 
    else if (strcmp(command, "run") == 0) {
        if (argc != 0) {
            printf("Unknown arguments for run\n");
            return 1;
        }
        request.command = BCP_RUN_FIRMWARE;
    }
    else if (strcmp(command, "version") == 0) {
        if (argc != 0) {
            printf("Unknown arguments for version\n");
            return 1;
        }
        request.command = BCP_GET_VERSION;
    }
    else {
        printf("Unknown command: %s\n", command);
        return 1;
    }

    request.crc = bcp_request_calculate_crc16(&request);
    
    char serial_port[256] = "/dev/ttyACM0";
    if (argc > 0) {
        strcpy(serial_port, argv[0]);
    }

    int fd = open(serial_port, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        printf("Error while opening %s\n", serial_port);
        return 1;
    }

    serial_port_init(fd, B115200, 0, true);

    bcp_send_request(fd, &request);
    
    bcp_response_t response;
    bcp_response_init(&response);

    if (bcp_get_response(fd, &response) == 0) {
        printf("An error occurred with response\n");
        return 1;
    }

    bcp_response_print(&response);

    uint16_t expected = bcp_response_calculate_crc16(&response);
    if (expected != response.crc) {
        printf("CRC error: got %02x (%02x expected)", response.crc, expected);
        return 1;
    }
    return 0;
}