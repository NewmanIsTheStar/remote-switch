/**
 * Copyright (c) 2024 NewmanIsTheStar
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef MQTT_H
#define MQTT_H

#include "FreeRTOS.h"

#define MQTT_TASK_LOOP_DELAY    (10000)


// mqtt_task.c
void mqtt_task(__unused void *params);




#endif
