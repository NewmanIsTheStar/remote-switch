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
#include "mqtt.h"
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
} MQTT_INITIALIZATION_T;

// prototypes
int mqtt_sanitize_user_config(void);
int mqtt_initialize(void);
int mqtt_deinitialize(int (*subsytem_init_func)(void));
int mqtt_initialize_something(void);

// external variables
extern uint32_t unix_time;
extern NON_VOL_VARIABLES_T config;
extern WEB_VARIABLES_T web;

// global variables
MQTT_INITIALIZATION_T mqtt_initialization_table[] =
{
    {mqtt_initialize_something,                false},           
};
bool something_initialized = false;

/*!
 * \brief Monitor temperature and control hvac system based on schedule
 *
 * \param params unused garbage
 * 
 * \return nothing
 */
void mqtt_task(void *params)
{
    printf("MQTT task started!\n");

    // check and correct critical user configuration settings
    mqtt_sanitize_user_config();
     
    while (true)
    {
         
        // initialize all subsystems that are not already up
        mqtt_initialize();

        printf("MQTT\n");
        
        // wait for timeout period or user change
        SLEEP_MS(MQTT_TASK_LOOP_DELAY); 
    }


    // tell watchdog task that we are still alive
    watchdog_pulse((int *)params);                   
}



/*!
 * \brief initialize subsystems
 *
 * \param params none
 * 
 * \return 0 on success
 */
int mqtt_initialize(void)
{
    int err = 0;
    int i;

    for (i=0; i < NUM_ROWS(mqtt_initialization_table); i++)
    {
        if (!mqtt_initialization_table[i].initialization_complete)
        {
            mqtt_initialization_table[i].initialization_complete = !mqtt_initialization_table[i].initialization();

            if (!mqtt_initialization_table[i].initialization_complete)
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
int mqtt_deinitialize(int (*subsytem_init_func)(void))
{
    int err = 1;
    int i;

    for (i=0; i < NUM_ROWS(mqtt_initialization_table); i++)
    {
        if (mqtt_initialization_table[i].initialization == subsytem_init_func)
        {
            mqtt_initialization_table[i].initialization_complete = false;
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
int mqtt_sanitize_user_config(void)
{   

    return(0);
}

 /*!
 * \brief perform sanity check on critical user config values
 *
 * \param params none
 * 
 * \return 0 on success
 */
int mqtt_initialize_something(void)
{   
    something_initialized = true;

    return(0);
}