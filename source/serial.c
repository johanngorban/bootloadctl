#include "serial.h"
#include <termios.h>
#include <errno.h>
#include <stdio.h>

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
    tty.c_cc[VMIN]  = blocking ? 1 : 0;  /* block until byte arrives or timeout */
    tty.c_cc[VTIME] = 30;                 /* 3 s read timeout (units: 100 ms)    */

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