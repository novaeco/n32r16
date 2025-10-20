#pragma once

#include <stdbool.h>

#include "data_model.h"

void t_io_start(void);
bool t_io_post_command(const io_command_t *cmd);

