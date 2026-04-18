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
#include "lwip/apps/mqtt.h"

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
void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status);
void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len); 
void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags);
void mqtt_sub_request_cb(void *arg, err_t result);
void start_mqtt_sub(mqtt_client_t *client);
void mqtt_pub_request_cb(void *arg, err_t result);
void rmtsw_mqtt_publish(mqtt_client_t *client, void *arg);

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
ip_addr_t broker_ip;


// static variables
static mqtt_client_t *mqtt_client;


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
        rmtsw_mqtt_publish(mqtt_client, NULL);
        
        // wait for timeout period or user change
        SLEEP_MS(MQTT_TASK_LOOP_DELAY); 

        // tell watchdog task that we are still alive
        watchdog_pulse((int *)params);           
    }              
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
    int err = -1;
    
    IP4_ADDR(&broker_ip, 192, 168, 22, 130); // broker IP

    struct mqtt_connect_client_info_t ci = 
    {
        .client_id = "pi_pico2w_client",
        .client_user = "bob",
        .client_pass = "monkey",
        .keep_alive = 60
    };

    mqtt_client = mqtt_client_new();
    
    if (mqtt_client != NULL) 
    {
        err = mqtt_client_connect(mqtt_client, &broker_ip, MQTT_PORT, mqtt_connection_cb, NULL, &ci);
    }

    something_initialized = true;

    return(err);
}


// Callback for connection status
void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    if (status == MQTT_CONNECT_ACCEPTED)
    {
        printf("MQTT Connected!\n");
        // Subscribe to a topic here
        //mqtt_sub_unsub(client, "homeassistant/#", 0, NULL, NULL, 1);

        start_mqtt_sub(client);

    } else
    {
        printf("MQTT Connection failed: %d\n", status);
    }
}

/*SUBSCRIBE************************************************************************************/
// 1. Publish Callback: Receives the topic
void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len) 
{
  printf("Topic: %s, Total Length: %u\n", topic, (unsigned int)tot_len);
}

// 2. Data Callback: Receives payload chunks
void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) 
{
  if(flags & MQTT_DATA_FLAG_LAST)
  {
    printf("Final message received: %.*s\n", len, (const char*)data);
  }
}

// 3. Sub Request Callback: Confirms subscription status
void mqtt_sub_request_cb(void *arg, err_t result) 
{
  printf("Subscribe result: %d\n", result);
}

// 4. Setup
void start_mqtt_sub(mqtt_client_t *client)
{
  // Set callbacks
  mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, NULL);

  // Subscribe
  mqtt_subscribe(client, "homeassistant/#", 1, mqtt_sub_request_cb, NULL);
}

/*PUBLISH**********************************************************************************************/
// 1. Define callback for publish completion
void mqtt_pub_request_cb(void *arg, err_t result) 
{
    if(result != ERR_OK) 
    {
        printf("Publish failed: %d\n", result);
    } else 
    {
        printf("Publish success\n");
    }
}

// 2. Example publish function
void rmtsw_mqtt_publish(mqtt_client_t *client, void *arg)
{
    const char *pub_payload = "Pico2W Hello!";
    err_t err;
    u8_t qos = 1; // 0, 1, or 2
    u8_t retain = 0;

    err = mqtt_publish(client, "test/test", pub_payload, strlen(pub_payload), qos, retain, mqtt_pub_request_cb, arg);

    if(err != ERR_OK) 
    {
        printf("Publish error: %d\n", err);
    }
}
