/**
 * Copyright (c) 2024 NewmanIsTheStar
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"
//#include "hardware/rtc.h"
#include "hardware/watchdog.h"
#include <hardware/flash.h>
#include "hardware/i2c.h"

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "timers.h"
#include "queue.h"

#include "stdarg.h"

#include "watchdog.h"
#include "weather.h"
#include "rmtsw.h"
#include "flash.h"
#include "calendar.h"
#include "utility.h"
#include "config.h"
#include "led_strip.h"
#include "message.h"
#include "altcp_tls_mbedtls_structs.h"
#include "powerwall.h"
#include "pluto.h"
#include "tm1637.h"


#define MINUTES_IN_WEEK (10080)

typedef struct
{
    TimerHandle_t timer_handle;
    bool timer_expired;
} CLIMATE_TIMERS_T;

typedef enum
{
    HVAC_LOCKOUT_TIMER   = 0,
    HVAC_OVERSHOOT_TIMER = 1,   
    HVAC_FAULT_TIMER     = 2,     
    NUM_HVAC_TIMERS      = 3
} CLIMATE_TIMER_INDEX_T;

typedef enum
{
    SETPOINT_BIAS_UNDEFINED = 0,
    SETPOINT_BIAS_HEATING   = 1,   
    SETPOINT_BIAS_COOLING   = 2
} SETPOINT_BIAS_T;

// prototypes
int set_hvac_gpio(THERMOSTAT_STATE_T thermostat_state);
int hvac_timer_start(CLIMATE_TIMER_INDEX_T timer_index, int minutes);
int hvac_timer_stop(CLIMATE_TIMER_INDEX_T timer_index);
bool hvac_timer_expired(CLIMATE_TIMER_INDEX_T timer_index);
void vTimerCallback(TimerHandle_t xTimer);
int update_current_setpoints(THERMOSTAT_STATE_T last_active, long int temperaturex10, int temporary_set_point_offsetx10);
int update_relay_scheduled_actions(void);
long int sanitize_setpoint(long int setpoint);
bool temporary_setpoint_offset_changed(void);
int thermostat_relay_lockout_stop(void);

// external variables
extern uint32_t unix_time;
extern NON_VOL_VARIABLES_T config;
extern WEB_VARIABLES_T web;
//extern long int temperaturex10;

// gloabl variables
static THERMOSTAT_MODE_T scheduled_mode = HVAC_AUTO;         // scheduled mode
static CLIMATE_TIMERS_T climate_timers[NUM_HVAC_TIMERS];     // set of timers used to control state
static bool relay_gpio_ok = false;                           // ok to use configured gpio

/*!
 * \brief Open or close relay based on user request or schedule
 * 
 * \return relay state where each bit represents one relay
 */
uint32_t rmtsw_relay_control(void)
{
    static bool relay_state[8] = {0,0,0,0,0,0,0,0};
    static uint32_t relay_timestamp[8] = {0,0,0,0,0,0,0,0};
    static uint32_t relay_delay[8] = {1,1,1,1,1,1,1,1};    
    int i;

    // update desired states based on schedule
    update_relay_scheduled_actions();

    CLIP(config.rmtsw_relay_max, 0, 8);

    // apply desired states
    for(i=0; i< config.rmtsw_relay_max; i++)
    {
        printf("desired[%d] = %d\n", i, web.rmtsw_relay_desired_state[i]);

        if (web.rmtsw_relay_enabled[i])
        {
            // check if state change requested
            if (relay_state[i] != web.rmtsw_relay_desired_state[i])
            {
                // check if sufficient time has passed since last state change
                if ((unix_time - relay_timestamp[i]) >  relay_delay[i])
                {
                    gpio_put(config.rmtsw_relay_gpio[i], web.rmtsw_relay_desired_state[i]);
                    relay_state[i] = web.rmtsw_relay_desired_state[i]; 
                    relay_timestamp[i] = unix_time; 
                    if (relay_delay[i] < 5*60)
                    {
                        relay_delay[i] *= 2; // double delay up to max of approx 5 minutes
                    }  
                }
            }
            else
            {
                // check if sufficient time has passed to reduce the delay
                if ((unix_time - relay_timestamp[i]) >  (2*relay_delay[i]))
                {
                    if (relay_delay[i] > 1)
                    {
                        relay_delay[i] /= 2; // halve delay down to min of 1 second
                    }                     
                }
            }          
        }
    }
                                   
    return(0);
}

/*!
 * \brief Start timer
 * 
 * \return non-zero on error
 */
int hvac_timer_start(CLIMATE_TIMER_INDEX_T timer_index, int minutes)
{
    int err = 0;          

    if (climate_timers[HVAC_LOCKOUT_TIMER].timer_handle)
    {
        if (minutes > 0)
        {
            // begin new lockout period  TODO error checking
            climate_timers[HVAC_LOCKOUT_TIMER].timer_expired = false;
            xTimerChangePeriod(climate_timers[HVAC_LOCKOUT_TIMER].timer_handle, minutes*60*1000, 1000);
            xTimerStart(climate_timers[HVAC_LOCKOUT_TIMER].timer_handle, 1000);
        }
    }
    else
    {
        err = 1;
    }

    return(err);
}

/*!
 * \brief Stop timer
 * 
 * \return true if timer expired
 */
int hvac_timer_stop(CLIMATE_TIMER_INDEX_T timer_index)
{
    int err = 0;         
 
    xTimerStart(climate_timers[HVAC_LOCKOUT_TIMER].timer_handle, 0);
    climate_timers[timer_index].timer_expired = true;
    
    return(err);
}

/*!
 * \brief Check timer
 * 
 * \return true if timer expired
 */
bool hvac_timer_expired(CLIMATE_TIMER_INDEX_T timer_index)
{
    bool expired = false;          
 
    expired = climate_timers[timer_index].timer_expired;
    
    return(expired);
}

/*!
 * \brief Set HVAC Control GPIOs
 * 
 * \return 
 */
int set_hvac_gpio(THERMOSTAT_STATE_T thermostat_state)
{
    int err = 0;

    // set the gpio output connected to the relay    
    //if (config.thermostat_enable && gpio_valid(config.heating_gpio) && gpio_valid(config.cooling_gpio) && gpio_valid(config.fan_gpio))
    if (config.thermostat_enable && relay_gpio_ok)
    {
        switch (thermostat_state)
        {
        case EXCESSIVE_OVERSHOOT:        
        case HEATING_AND_COOLING_OFF:
            printf("Heating, Cooling and Fan off\n");
            gpio_put(config.heating_gpio, 0);
            gpio_put(config.cooling_gpio, 0);
            gpio_put(config.fan_gpio, 0);   
            break;
        case HEATING_IN_PROGRESS:
            printf("Heating on, Cooling and Fan off\n");
            gpio_put(config.heating_gpio, 1);
            gpio_put(config.cooling_gpio, 0);
            gpio_put(config.fan_gpio, 0);      // when heating the thermostat is *not* responsible for turning on the fan (to avoid blowing cold air)
            break;
        case COOLING_IN_PROGRESS:
            printf("Cooling on, Fan on and Heating off\n");
            gpio_put(config.heating_gpio, 0);
            gpio_put(config.cooling_gpio, 1);
            gpio_put(config.fan_gpio, 1);      // it is conventional for the thermostat to turn on the fan when cooling
            break;                 
        case FAN_ONLY_IN_PROGRESS:
            printf("Fan on, Cooling and Heating off\n");
            gpio_put(config.heating_gpio, 0);
            gpio_put(config.cooling_gpio, 0);
            gpio_put(config.fan_gpio, 1);      // air circulation only
            break;                    
        case DUCT_PURGE:
            printf("Heating and Cooling off and Fan on\n");
            gpio_put(config.heating_gpio, 0);
            gpio_put(config.cooling_gpio, 0);
            gpio_put(config.fan_gpio, 1);   
            break; 
        default:
        case THERMOSTAT_LOCKOUT:
            err = 1;
            break;         
        }

        hvac_log_state_change(thermostat_state);
    }
    else
    {
        err = 1;
    } 

    return(err);
}

/*!
 * \brief Timer callback
 *
 * \param[in]   timer handle      handle of timer that expired
 * 
 * \return nothing
 */
void vTimerCallback(TimerHandle_t xTimer)
 {
    int id;

    if (xTimer)
    {
        id = (int)pvTimerGetTimerID(xTimer);

        if ((id >= 0) && (id < NUM_HVAC_TIMERS))
        {
            climate_timers[id].timer_expired = true;
        }
    }
 }

int rmtsw_relay_initialize(void)
{
    int err = 0;
    int i;

    if (relay_gpio_ok)
    {
        // // create hvac timers
        // for (i=0; i < NUM_HVAC_TIMERS; i++)
        // {
        //     climate_timers[i].timer_handle = xTimerCreate("Timer", 1000, pdFALSE, (void *)i, vTimerCallback);  

        //     //printf("Created timer with handle = %p\n", climate_timers[i].timer_handle);
        // }

        CLIP(config.rmtsw_relay_max, 0, 8);

        for(i=0; i<config.rmtsw_relay_max; i++)
        {
            if (web.rmtsw_relay_enabled[i])
            {
                gpio_init(config.rmtsw_relay_gpio[i]);
                gpio_put(config.rmtsw_relay_gpio[i], 0);
                gpio_set_dir(config.rmtsw_relay_gpio[i], true);                
            }
        }        
        err = 0;
    }
    else
    {
        err = 1;
    }

    return(err);
}

int rmtsw_relay_gpio_enable(bool enable)
{
    relay_gpio_ok = enable;
}

long int sanitize_setpoint(long int setpoint)
{
    // sanitize setpoint
    if (config.use_archaic_units)
    {
        // fahrenheit between 60 and 90
        if ((setpoint > SETPOINT_MAX_FAHRENHEIT_X_10) || (setpoint < SETPOINT_MIN_FAHRENHEIT_X_10))
        {
            setpoint = SETPOINT_DEFAULT_FAHRENHEIT_X_10;
        }
    }
    else
    {
        // celsius between 15 and 32
        if ((setpoint > SETPOINT_MAX_CELSIUS_X_10) || (setpoint < SETPOINT_MIN_CELSIUS_X_10))
        {
            setpoint = SETPOINT_DEFAULT_CELSIUS_X_10;
        }
    }        

    return(setpoint);
}


int thermostat_relay_lockout_stop(void)
{
    hvac_timer_stop(HVAC_LOCKOUT_TIMER);
    hvac_timer_stop(HVAC_OVERSHOOT_TIMER);
      
    return(0);
}

/*!
 * \brief  Update the relay state based on schedule
 * 
 * \return nothing
 */
int update_relay_scheduled_actions(void)
{
    int err = 0;
    int i;
    int mow = -1;
    int candidate_start_mow = -1;
    int candidate_off_bitmap = 0;
    int candidate_on_bitmap = 0;
    int candidate_delta = 0;
    static int current_start_mow = 0;
    int delta = 10079;  // number of minutes in a week

    // get relay state according to schedule
    if (!get_mow_local_tz(&mow))
    {
        for(i=0; i < NUM_ROWS(config.rmtsw_relay_schedule_start_mow); i++)
        {
            
            if (rmtsw_schedule_row_valid(i))
            {

                candidate_delta = mow_future_delta(config.rmtsw_relay_schedule_start_mow[i], mow);

                if (candidate_delta < delta)
                {
                    delta = candidate_delta;

                    candidate_start_mow  = config.rmtsw_relay_schedule_start_mow[i];
                    candidate_off_bitmap = config.rmtsw_relay_schedule_action_off[i];
                    candidate_on_bitmap  = config.rmtsw_relay_schedule_action_on[i]; 
                }
            }
        }
    }

    // check if we've entered a new schedule period
    if ((candidate_start_mow >= 0) && (candidate_start_mow < MINUTES_IN_WEEK) && (current_start_mow != candidate_start_mow))
    {
        current_start_mow = candidate_start_mow;

        // update relay states based on schedule
        for(i=0; i<8; i++)
        {
            if (candidate_off_bitmap & (1<<i))
            {
                web.rmtsw_relay_desired_state[i] = false;
            }
            else if (candidate_on_bitmap & (1<<i))
            {
                web.rmtsw_relay_desired_state[i] = true;
            }
        }

        printf("New scheduled relay state.  start mow = %d off = %08b on = %08b\n", candidate_start_mow, candidate_off_bitmap, candidate_on_bitmap);
    }  

    return(err);
}
