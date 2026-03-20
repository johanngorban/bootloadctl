#pragma once

#include "bcp.h"
#include <stdbool.h>

typedef struct {
    char firmware_path[256];
    int  serial_fd;
} context_t;

void handle_response(const context_t *context, const bcp_response_t *response, bool debug);
