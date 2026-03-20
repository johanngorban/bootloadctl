#pragma once
#include <stdint.h>

void gmodem_send(int fd, const uint8_t *bytes, uint32_t length);
