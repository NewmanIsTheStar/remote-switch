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
    {rmtsw_initialize_relays,                false},           
};
bool buttons_initialized = false;

/*!
 * \brief Monitor temperature and control hvac system based on schedule
 *
 * \param params unused garbage
 * 
 * \return nothing
 */
void rmtsw_task(void *params)
{
    int ath10_error = 0;
    int tm1637_error = 0;
    int i2c_bytes_written = 0;
    int i2c_bytes_read = 0;
    long int temperaturex10 = 0;
    long int humidityx10 = 0;
    long int moving_avaerage_temperaturex10;
    int retry = 0;
    int oneshot = false;
    int i;
    bool button_pressed = false;

    if (strcasecmp(APP_NAME, "remote-switch") == 0)
    {
        // single purpose application -- force personality and enable
        config.personality = REMOTE_SWITCH;
        config.thermostat_enable = 1;
    }

    printf("Remote Switch (rmtsw) task started!\n");

    // set initial status
    // temperaturex10 = thermostat_get_default_temperature();   
    // web.powerwall_grid_status = GRID_UNKNOWN;

    // check and correct critical user configuration settings
    rmtsw_sanitize_user_config();

    // create the schedule grid used in web inteface
    make_schedule_grid();
     
    while (true)
    {
        if ((config.personality == REMOTE_SWITCH))  // TODO should this be config.remote_switch_enable ?
        {
            // check user configured gpios
            rmtsw_validate_gpio_set();
            
            // initialize all subsystems that are not already up
            rmtsw_initialize();

            // set hvac relays
            rmtsw_relay_control(temperaturex10);

            // if (buttons_initialized)
            // {
            //     // process button presses until a period of inactivity occurs
            //     button_pressed = handle_button_press_with_timeout(THERMOSTAT_TASK_LOOP_DELAY);
            // }
            // else
            {
                SLEEP_MS(THERMOSTAT_TASK_LOOP_DELAY); 
            }

            // update web schedule
            make_schedule_grid();
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
    // int gpio_list[10];
    // bool relay_gpio_valid = false;
    // bool ath10_gpio_valid = false;
    // bool display_gpio_valid = false;
    // bool button_gpio_valid = false;    

    // // relays
    // gpio_list[0] = config.cooling_gpio;
    // gpio_list[1] = config.heating_gpio;
    // gpio_list[2] = config.fan_gpio;

    // // temperature sensor
    // gpio_list[3] = config.thermostat_temperature_sensor_clock_gpio;
    // gpio_list[4] = config.thermostat_temperature_sensor_data_gpio;

    // // display
    // gpio_list[5] = config.thermostat_seven_segment_display_clock_gpio;
    // gpio_list[6] = config.thermostat_seven_segment_display_data_gpio;

    // // front panel buttons
    // gpio_list[7] = config.thermostat_increase_button_gpio;
    // gpio_list[8] = config.thermostat_decrease_button_gpio;
    // gpio_list[9] = config.thermostat_mode_button_gpio;

    // // check for gpio conflicts
    // if (!gpio_conflict(gpio_list, 10))
    // {
    //     // no conflicts
    //     relay_gpio_valid = true;
    //     ath10_gpio_valid = true;
    //     display_gpio_valid = true;
    //     button_gpio_valid = true;
    // }
    // else
    // {
    //     // conflicts found
    //     relay_gpio_valid = false;
    //     ath10_gpio_valid = false;
    //     display_gpio_valid = false;
    //     button_gpio_valid = false;

    //     // incrementally expand list to find non-conflicting functions
    //     if (gpio_conflict(gpio_list, 3))
    //     {
    //         relay_gpio_valid = true;
    //     } 

    //     if (gpio_conflict(gpio_list, 5))
    //     {
    //         ath10_gpio_valid = true;
    //     }   
        
    //     if (gpio_conflict(gpio_list, 7))
    //     {
    //         display_gpio_valid = true;
    //     }  
        
    //     if (gpio_conflict(gpio_list, 10))
    //     {
    //         button_gpio_valid = true;
    //     }         
    // }

    // // check gpios are valid
    // if (!gpio_valid(config.cooling_gpio) || !gpio_valid(config.heating_gpio) || !gpio_valid(config.fan_gpio))
    // {
    //     relay_gpio_valid = false;
    // }

    // if (!gpio_valid(config.thermostat_temperature_sensor_clock_gpio) || !gpio_valid(config.thermostat_temperature_sensor_data_gpio))
    // {
    //     ath10_gpio_valid = false;
    // }

    // if (!gpio_valid(config.thermostat_seven_segment_display_clock_gpio) || !gpio_valid(config.thermostat_seven_segment_display_data_gpio))
    // {
    //     display_gpio_valid = false;
    // }

    // if (!gpio_valid(config.thermostat_increase_button_gpio) || !gpio_valid(config.thermostat_decrease_button_gpio) || !gpio_valid(config.thermostat_mode_button_gpio))
    // {
    //     button_gpio_valid = false;
    // }    

    // // tell subsystems they can use gpio
    // rmtsw_relay_gpio_enable(relay_gpio_valid);
    // ath10_gpio_enable(ath10_gpio_valid);
    // display_gpio_enable(display_gpio_valid);
    // button_gpio_enable(button_gpio_valid);

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

    //button_error = initialize_relays(config.thermostat_mode_button_gpio, config.thermostat_increase_button_gpio, config.thermostat_decrease_button_gpio);    

    if (!relay_error)
    {
        buttons_initialized = true;
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