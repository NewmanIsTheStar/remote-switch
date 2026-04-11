/**
 * Copyright (c) 2024 NewmanIsTheStar
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef REMOTE_SWITCH_H
#define REMOTE_SWITCH_H

#include "FreeRTOS.h"

#define REMOTE_SWITCH_TASK_LOOP_DELAY    (10000)
#define SETPOINT_DEFAULT_CELSIUS_X_10    (210)      // 21.0 C
#define SETPOINT_MAX_CELSIUS_X_10        (320)      // 32.0 C
#define SETPOINT_MIN_CELSIUS_X_10        (150)      // 15.0 C 
#define SETPOINT_DEFAULT_FAHRENHEIT_X_10 (700)      // 70.0 F
#define SETPOINT_MAX_FAHRENHEIT_X_10     (900)      // 90.0 F
#define SETPOINT_MIN_FAHRENHEIT_X_10     (600)      // 60.0 F
#define SETPOINT_TEMP_UNDEFINED          (-10001)
#define SETPOINT_TEMP_INVALID_FAN        (-10002)
#define SETPOINT_TEMP_INVALID_OFF        (-10003)
#define SETPOINT_TEMP_DEFAULT_C          (210)
#define SETPOINT_TEMP_DEFAULT_F          (700)
#define DISPLAY_MAX_BRIGHTNESS           (7)
#define TEMPERATURE_INVALID              (2000)


typedef enum
{
    RMSW_ACTION_DO_NOTHING = 0,
    RMSW_ACTION_OFF = 1,
    RMSW_ACTION_ON = 2,
    NUM_RMSW_ACTIONS = 3
} RMSW_ACTION_T;

typedef enum
{
    HVAC_AUTO = 0,
    HVAC_OFF = 1,
    HVAC_HEATING_ONLY = 2,
    HVAC_COOLING_ONLY = 3,
    HVAC_FAN_ONLY = 4,
    HVAC_HEAT_AND_COOL = 5,
    NUM_HVAC_MODES = 6
} THERMOSTAT_MODE_T;          // set by user -- do not reorder -- html and ssi.c rely on order

typedef enum
{
    HEATING_AND_COOLING_OFF = 0,
    HEATING_IN_PROGRESS = 1,
    COOLING_IN_PROGRESS = 2,
    FAN_ONLY_IN_PROGRESS = 3,
    DUCT_PURGE = 4,
    THERMOSTAT_LOCKOUT = 5,
    EXCESSIVE_OVERSHOOT = 6,
    FAULT_LOCKOUT = 7,
} THERMOSTAT_STATE_T;         // operational state

typedef enum
{
    COOLING_LAG = 0,
    HEATING_LAG = 1,   
    NUM_MOMENTUMS   = 2
} CLIMATE_LAG_T;

typedef struct
{
    TickType_t change_tick;
    THERMOSTAT_STATE_T new_state;      
    int change_temperature;   
} HVAC_STATE_CHANGE_LOG_T;

typedef struct
{
    uint32_t unix_time;   // TODO: should be using time_t
    long int temperaturex10;
    long int humidityx10;
} CLIMATE_DATAPOINT_T;

// thermostat_task.c
void rmtsw_task(__unused void *params);
int make_schedule_grid(void);
//int update_current_setpoints(void);
int copy_schedule(int source_day, int destination_day);


// thermostat_web_ui.c
int get_free_schedule_row(void);
bool schedule_row_valid(int row);
bool day_compare(int day1, int day2);
void hvac_log_state_change(THERMOSTAT_STATE_T new_state);
bool schedule_setpoint_valid(long int temperaturex10, long int heating_temperaturex10, long int cooling_temperaturex10, int mow, THERMOSTAT_MODE_T mode);
bool schedule_mow_valid(int mow);
bool schedule_mode_valid(int mode);

// rmtsw_relay.c
int rmtsw_relay_initialize(void);
uint32_t rmtsw_relay_control(void);
int rmtsw_relay_gpio_enable(bool enable);



#endif
