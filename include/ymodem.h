#pragma once
#include <stdint.h>

void ymodem_send(int fd, const uint8_t *bytes, uint16_t length);
