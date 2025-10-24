#pragma once

#include "data_model.h"
#include "onewire_bus.h"

void sensors_task_start(sensor_data_model_t *model, onewire_bus_handle_t bus);
