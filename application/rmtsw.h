/**
 * Copyright (c) 2024 NewmanIsTheStar
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef REMOTE_SWITCH_H
#define REMOTE_SWITCH_H

#include "FreeRTOS.h"

#define REMOTE_SWITCH_TASK_LOOP_DELAY    (10000)


typedef enum
{
    RMSW_ACTION_DO_NOTHING = 0,
    RMSW_ACTION_OFF = 1,
    RMSW_ACTION_ON = 2,
    NUM_RMSW_ACTIONS = 3
} RMSW_ACTION_T;



// rmtsw_task.c
void rmtsw_task(__unused void *params);
int rmtsw_copy_schedule(int source_day, int destination_day);

// rmtsw_web_ui.c
int rmtsw_get_free_schedule_row(void);
bool rmtsw_schedule_row_valid(int row);
bool rmtsw_day_compare(int day1, int day2);

// rmtsw_relay.c
int rmtsw_relay_initialize(void);
uint32_t rmtsw_relay_control(void);
int rmtsw_relay_gpio_enable(bool enable);
int rmtsw_sort_schedule(void);
int rmtsw_wait(TickType_t timeout);
void rmtsw_queue_send(uint8_t message);
int rmtsw_initialize_queue(void);


#endif
