#pragma once

#include "fwp.h"
#include <stdint.h>

fwp_status_t fwp_transmit(int fd, const uint8_t *data, uint32_t length);
