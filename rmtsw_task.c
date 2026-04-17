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

// typdedefs
typedef struct
{
    int (*initialization)(void);
    bool initialization_complete;
} THERMOSTAT_INITIALIZATION_T;

// prototypes
int rmtsw_sanitize_user_config(void);
int rmtsw_initialize(void);
int rmtsw_deinitialize(int (*subsytem_init_func)(void));
int rmtsw_initialize_relays(void);
int rmtsw_validate_gpio_set(void);

// external variables
extern uint32_t unix_time;
extern NON_VOL_VARIABLES_T config;
extern WEB_VARIABLES_T web;

// global variables
THERMOSTAT_INITIALIZATION_T initialization_table[] =
{
    {rmtsw_initialize_queue,                 false},  
    {rmtsw_initialize_relays,                false},           
};
bool relays_initialized = false;

/*!
 * \brief Monitor temperature and control hvac system based on schedule
 *
 * \param params unused garbage
 * 
 * \return nothing
 */
void rmtsw_task(void *params)
{
    int retry = 0;
    int oneshot = false;
    int i;

    if (strcasecmp(APP_NAME, "remote-switch") == 0)
    {
        // single purpose application -- force personality and enable
        config.personality = REMOTE_SWITCH;
        config.rmtsw_enable = 1;
    }

    printf("Remote Switch (rmtsw) task started!\n");

    // set initial status
    memset(web.rmtsw_relay_desired_state, 0, sizeof(web.rmtsw_relay_desired_state));

    // check and correct critical user configuration settings
    rmtsw_sanitize_user_config();
     
    while (true)
    {
        if ((config.personality == REMOTE_SWITCH))  // TODO should this be config.remote_switch_enable ?
        {
            // check user configured gpios
            rmtsw_validate_gpio_set();
            
            // initialize all subsystems that are not already up
            rmtsw_initialize();

            // set relays
            rmtsw_relay_control();

            // wait for timeout period or user change
            rmtsw_wait(REMOTE_SWITCH_TASK_LOOP_DELAY);
            //SLEEP_MS(REMOTE_SWITCH_TASK_LOOP_DELAY); 
        }
        else
        {
            SLEEP_MS(60000); 
        }   

        // tell watchdog task that we are still alive
        watchdog_pulse((int *)params);               
    }
}

/*!
 * \brief Validate set of GPIOs
 *
 * \param params max_set
 * 
 * \return nothing
 */
int rmtsw_validate_gpio_set(void)
{
    int i;
    int j;
    bool relay_gpio_valid = false;

    if (config.rmtsw_relay_max >= 1)
    {
        // check for gpio conflicts
        if (!gpio_conflict(config.rmtsw_relay_gpio, config.rmtsw_relay_max))
        {
            // no conflicts
            i = NUM_ROWS(config.rmtsw_relay_gpio);
        }
        else
        {
            // conflicts found
            relay_gpio_valid = false;

            // search for first conflict
            for(i=0; i<config.rmtsw_relay_max; i++)
            {
                if (gpio_conflict(config.rmtsw_relay_gpio, i))
                {
                    break;
                }
            }
        }

        // enable all gpios prior to first conflict
        for (j=0; j<config.rmtsw_relay_max; j++)
        {
            if ((j < i) && gpio_valid(config.rmtsw_relay_gpio[j]))
            {
                web.rmtsw_relay_enabled[j] = 1;
                relay_gpio_valid = true;            
            }
            else
            {
                web.rmtsw_relay_enabled[j] = 0;
            }
        } 

        // tell subsystems they can use gpio
        rmtsw_relay_gpio_enable(relay_gpio_valid);
    }
    return(0);
}



/*!
 * \brief initialize front panel buttons
 *
 * \param params none
 * 
 * \return 0 on success
 */
int rmtsw_initialize_relays(void)
{
    int relay_error = 0;

    relay_error = rmtsw_relay_initialize();

    if (!relay_error)
    {
        relays_initialized = true;
    }

    return(relay_error);
}

/*!
 * \brief initialize subsystems
 *
 * \param params none
 * 
 * \return 0 on success
 */
int rmtsw_initialize(void)
{
    int err = 0;
    int i;

    for (i=0; i < NUM_ROWS(initialization_table); i++)
    {
        if (!initialization_table[i].initialization_complete)
        {
            initialization_table[i].initialization_complete = !initialization_table[i].initialization();

            if (!initialization_table[i].initialization_complete)
            {
                err++;
                printf("Error initializing subsystem %d\n", i);
            }
        }
    }

    if (err)
    {
        printf("%d subsystems failed to initialize\n", err);
    }

    return(err);
}

/*!
 * \brief deinitialize a subsytem
 *
 * \param params none
 * 
 * \return 0 on success
 */
int rmtsw_deinitialize(int (*subsytem_init_func)(void))
{
    int err = 1;
    int i;

    for (i=0; i < NUM_ROWS(initialization_table); i++)
    {
        if (initialization_table[i].initialization == subsytem_init_func)
        {
            initialization_table[i].initialization_complete = false;
            err = 0;
            break;
        }
    }

    return(err);
}


 /*!
 * \brief perform sanity check on critical user config values
 *
 * \param params none
 * 
 * \return 0 on success
 */
int rmtsw_sanitize_user_config(void)
{   
    // // make sure safeguards are valid to prevent short cycling
    // CLIP(config.heating_to_cooling_lockout_mins, 1, 60);
    // CLIP(config.minimum_heating_on_mins, 1, 60);
    // CLIP(config.minimum_cooling_on_mins, 1, 60);
    // CLIP(config.minimum_heating_off_mins, 1, 60);
    // CLIP(config.minimum_cooling_off_mins, 1, 60);
    // if (config.use_archaic_units)
    // {
    //     CLIP(config.thermostat_hysteresis, 10, 100);  // 1 F to 10 F
    // }
    // else
    // {
    //     CLIP(config.thermostat_hysteresis, 5, 50);   // 0.5 C to 5 C       
    // }

    return(0);
}