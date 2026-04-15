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
//#include "weather.h"
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

// external variables
extern NON_VOL_VARIABLES_T config;
extern WEB_VARIABLES_T web;

// global variables
static int hvac_state_change_log_index = 0;
static HVAC_STATE_CHANGE_LOG_T hvac_state_change_log[32];

/*!
 * \brief Sort schedule
 * 
 * \return nothing
 */
int rmtsw_sort_schedule(void)
{
    int i, j, x, y;
    int key_mow = 0;
    int key_action_on = 0;
    int key_action_off = 0;

    // check for duplicates and remove
    CLIP(web.rmtsw_relay_period_row, 0, NUM_ROWS(config.rmtsw_relay_schedule_start_mow));
    for(i=0; i<NUM_ROWS(config.rmtsw_relay_schedule_start_mow); i++)
    {
        if ((i != web.rmtsw_relay_period_row) &&
            (config.rmtsw_relay_schedule_start_mow[i] >= 0) &&
            (config.rmtsw_relay_schedule_start_mow[i] == config.rmtsw_relay_schedule_start_mow[web.rmtsw_relay_period_row]))
        {
            printf("Duplicate relay period deleted\n");
            config.rmtsw_relay_schedule_start_mow[i] = -1;
        }
    }    

    // sort the schedule into ascending order by mow
    for(i=1; i<NUM_ROWS(config.rmtsw_relay_schedule_start_mow); i++)
    {
        key_mow = config.rmtsw_relay_schedule_start_mow[i];
        key_action_on = config.rmtsw_relay_schedule_action_on[i];  
        key_action_off = config.rmtsw_relay_schedule_action_off[i]; 

        j = i - 1;

        while ((j >= 0) && (config.rmtsw_relay_schedule_start_mow[j] > key_mow))
        {
            config.rmtsw_relay_schedule_start_mow[j+1] = config.rmtsw_relay_schedule_start_mow[j];
            config.rmtsw_relay_schedule_action_on[j+1] = config.rmtsw_relay_schedule_action_on[j]; 
            config.rmtsw_relay_schedule_action_off[j+1] = config.rmtsw_relay_schedule_action_off[j];           
            j = j - 1;
        }

        config.rmtsw_relay_schedule_start_mow[j+1] = key_mow;
        config.rmtsw_relay_schedule_action_on[j+1] = key_action_on; 
        config.rmtsw_relay_schedule_action_off[j+1] = key_action_off; 
            
    }

    return(0);
}


/*!
 * \brief Copy daily schedule to other day(s)
 * 
 * \return nothing
 */
int rmtsw_copy_schedule(int source_day, int destination_day)
{
    int i, j, k, x, y;
    int key_mow = 0;
    int key_temp = 0;
    //int mow;
    int mod;
    int setpointtemperaturex10 = 0;
    bool found = false;
    int populated_rows = 0;
    int mow[NUM_ROWS(config.rmtsw_relay_schedule_start_mow)];
    int temp[NUM_ROWS(config.rmtsw_relay_schedule_start_mow)];
    int day = 0;

    CLIP(source_day, 0, 6);

    // erase existing entries on destination days
    for(i=0; i<NUM_ROWS(config.rmtsw_relay_schedule_start_mow); i++)
    {
        if ((config.rmtsw_relay_schedule_start_mow[i] >= 0) && (config.rmtsw_relay_schedule_start_mow[i] < 60*24*7))
        {
            day = config.rmtsw_relay_schedule_start_mow[i]/(60*24);
            CLIP(day, 0, 6);
        }

        // 0-6 = sunday to saturday, 7 = everyday, 8 = weekdays, 9 = weekend days
        if ((day != source_day) && (!day_compare(day, destination_day)))                                                                      
        {
            // mark unused    
            config.rmtsw_relay_schedule_start_mow[i] = -1;
            config.rmtsw_relay_schedule_action_off[i] = 0;
            config.rmtsw_relay_schedule_action_on[i] = 0;
        }
    }


    for (day = 0; day < 7; day++)   // day to copy into -- loop needed for destinations like "every weekday"
    {
        for (i=0; i < NUM_ROWS(config.rmtsw_relay_schedule_start_mow); i++)  // scan existing schedule
        {
            if (rmtsw_schedule_row_valid(i))
            {
                j = config.rmtsw_relay_schedule_start_mow[i]/(60*24);
                mod = config.rmtsw_relay_schedule_start_mow[i]%(60*24);
                CLIP(j, 0, 6);
                CLIP(mod, 0, 60*24);

                // 0-6 = sunday to saturday, 7 = everyday, 8 = weekdays, 9 = weekend days
                if ((day != source_day) && (j == source_day) && (!day_compare(day, destination_day)))
                {
                    k = rmtsw_get_free_schedule_row();

                    if ((k >= 0) && (k < NUM_ROWS(config.rmtsw_relay_schedule_start_mow)))
                    {
                        // copy schedule
                        config.rmtsw_relay_schedule_start_mow[k] = day*(60*24) + mod;
                        config.rmtsw_relay_schedule_action_off[k] = config.rmtsw_relay_schedule_action_off[i];
                        config.rmtsw_relay_schedule_action_on[k] = config.rmtsw_relay_schedule_action_on[i];
                    }
                }
             }
        }
    }

    return(0);
}



/*!
 * \brief check if day matchs
 * 
 * \return return false if match, true if diffeent
 */
bool day_compare(int day1, int day2)
{
    bool diff = true;
    int multi_day = 0;
    int compare_day = 0;

    if (day1 == day2)
    {
        diff = false;
    }

    if (day1 > 6)
    {
        multi_day = day1;
        compare_day = day2;
    }
    else
    {
        multi_day = day2;
        compare_day = day1;    
    }

    switch(multi_day)
    {
        case 7: // everyday
            diff = false;
            break;
        case 8: // weekday
            if ((compare_day >=1) && (compare_day <= 5))
            {
                diff = false;
            } 
            break;
        case 9: // weekend
            if ((compare_day == 0) || (compare_day == 6))
            {
                diff = false;
            }
            break;
    }
    
    return(diff);
}

/*!
 * \brief check if temperature schedule row is valid
 * 
 * \return true if temperature schedule entry is valid
 */
bool rmtsw_schedule_row_valid(int row)
{
    bool valid = true;


    if ((row < 0) || (row > NUM_ROWS(config.rmtsw_relay_schedule_start_mow)))
    {
        valid = false;
    }
    else if ((config.rmtsw_relay_schedule_start_mow[row] < 0) ||  (config.rmtsw_relay_schedule_start_mow[row] > (60*24*7))) 
    {
        valid = false;
    }
  
    return(valid);
}


/*!
 * \brief Copy daily schedule to other day(s)
 * 
 * \return nothing
 */
int rmtsw_get_free_schedule_row(void)
{
    int i = 0;
    bool found = false;

    for(i=0; i<NUM_ROWS(config.rmtsw_relay_schedule_start_mow); i++)
    {
        if (!rmtsw_schedule_row_valid(i))
        {
            found = true;
            break;
        }
    }

    if (!found) i = -1;

    return(i);
}


 /*!
 * \brief Timer callback
 *
 * \param[in]   new_state      current thermostat state
 * 
 * \return nothing
 */
void hvac_log_state_change(THERMOSTAT_STATE_T new_state)
{
    static THERMOSTAT_STATE_T previous_state = DUCT_PURGE;

    if (new_state != previous_state)
    {
        hvac_state_change_log[hvac_state_change_log_index].change_tick = xTaskGetTickCount();
        hvac_state_change_log[hvac_state_change_log_index].new_state = new_state;
        hvac_state_change_log[hvac_state_change_log_index].change_temperature = web.thermostat_temperature;

        hvac_state_change_log_index = (hvac_state_change_log_index + 1) % NUM_ROWS(hvac_state_change_log);
    }
}
 


/*!
 * \brief check if schedule setpoint is valid
 * 
 * \return nothing
 */
bool schedule_setpoint_valid(long int temperaturex10, long int heating_temperaturex10, long int cooling_temperaturex10, int mow, THERMOSTAT_MODE_T mode)
{
    bool valid = false;


    if (schedule_mow_valid(mow) && schedule_mode_valid(mode))
    {
        switch(mode)
        {
        case HVAC_OFF:
        case HVAC_FAN_ONLY:
            valid = true; 
            break;
        default:
        case HVAC_AUTO:
        case HVAC_HEATING_ONLY:
        case HVAC_COOLING_ONLY:
            if (config.use_archaic_units)
            {
                if ((temperaturex10 > SETPOINT_MIN_FAHRENHEIT_X_10) && (temperaturex10 < SETPOINT_MAX_FAHRENHEIT_X_10))
                {
                    valid = true;
                }
            }
            else
            {
                if ((temperaturex10 > SETPOINT_MIN_CELSIUS_X_10) && (temperaturex10 < SETPOINT_MAX_CELSIUS_X_10))
                {
                    valid = true;
                }
            }
            break;
        case HVAC_HEAT_AND_COOL:
            if (config.use_archaic_units)
            {
                if (((heating_temperaturex10 > SETPOINT_MIN_FAHRENHEIT_X_10) && (heating_temperaturex10 < SETPOINT_MAX_FAHRENHEIT_X_10)) &&
                    ((cooling_temperaturex10 > SETPOINT_MIN_FAHRENHEIT_X_10) && (cooling_temperaturex10 < SETPOINT_MAX_FAHRENHEIT_X_10)))
                {
                    valid = true;
                }
            }
            else
            {
                if (((heating_temperaturex10 > SETPOINT_MIN_CELSIUS_X_10) && (heating_temperaturex10 < SETPOINT_MAX_CELSIUS_X_10)) &&
                    ((cooling_temperaturex10 > SETPOINT_MIN_CELSIUS_X_10) && (cooling_temperaturex10 < SETPOINT_MAX_CELSIUS_X_10)))
                {
                    valid = true;
                }
            }
            break;

        }
    }

    return(valid);
}

/*!
 * \brief check if schedule minute of week is valid
 * 
 * \return nothing
 */
bool schedule_mow_valid(int mow)
{
    bool valid = false; 

     if ((mow >= 0) && (mow < 60*24*7))
     {
        valid = true;
     }

     return(valid);
}

/*!
 * \brief check if schedule mode is valid
 * 
 * \return nothing
 */
bool schedule_mode_valid(int mode)
{
    bool valid = false; 

    switch(mode)
    {
    case HVAC_AUTO:
    case HVAC_OFF:
    case HVAC_HEATING_ONLY:
    case HVAC_COOLING_ONLY:
    case HVAC_FAN_ONLY:
    case HVAC_HEAT_AND_COOL:
    case NUM_HVAC_MODES:
        valid = true;
        break;
    default:
        valid = false;
        break;
    }

    return(valid);
}