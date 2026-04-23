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
int mqtt_initialize_connection(void);
void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status);
void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len); 
void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags);
void mqtt_sub_request_cb(void *arg, err_t result);
void start_mqtt_sub(mqtt_client_t *client);
void mqtt_pub_request_cb(void *arg, err_t result);
void rmtsw_mqtt_publish_discovery(mqtt_client_t *client, void *arg);
int mqtt_initialize_ha_discovery(void);
void rmtsw_mqtt_publish_state(int relay, mqtt_client_t *client, void *arg);
int rmtsw_mqtt_construct_discovery_topic(char *buffer, size_t len);
int rmtsw_mqtt_construct_discovery_payload(char *buffer, size_t len);

// external variables
extern uint32_t unix_time;
extern NON_VOL_VARIABLES_T config;
extern WEB_VARIABLES_T web;

// global variables
MQTT_INITIALIZATION_T mqtt_initialization_table[] =
{
    {mqtt_initialize_connection,                false},    
    {mqtt_initialize_ha_discovery,              false},             
};
bool connection_initialized = false;
bool discovery_initialized = false;
ip_addr_t broker_ip;
int relay_to_switch = -1;
int relay_desired_state = -1;

// static variables
static mqtt_client_t *mqtt_client;




const char ha_device_discovery_payload[] =
"{"
"  \"dev\": {"
"    \"ids\": \"rmtsw-00-11-22-33-44-55\","
"    \"name\": \"cluster-power\""
"  },"
"  \"o\": {"
"    \"name\":\"cluster-power\","
"    \"sw\": \"2.1\","
"    \"url\": \"https://bla2mqtt.example.com/support\""
"  },"
"  \"cmps\": {"
"    \"rmtsw-relay1-00-11-22-33-44-55\": {"
"      \"p\": \"switch\","
"      \"command_topic\":\"relay1/command\","
"      \"state_topic\":\"relay1/state\","   
"      \"unique_id\":\"monkey1\","
"      \"name\":\"RELAY-1\""
"    },"
"    \"rmtsw-relay2-00-11-22-33-44-55\": {"
"      \"p\": \"switch\","
"      \"command_topic\":\"relay2/command\","
"      \"state_topic\":\"relay2/state\","      
"      \"unique_id\":\"monkey2\","
"      \"name\":\"RELAY-2\""
"    },"
"    \"rmtsw-relay3-00-11-22-33-44-55\": {"
"      \"p\": \"switch\","
"      \"command_topic\":\"relay3/command\","
"      \"state_topic\":\"relay3/state\","      
"      \"unique_id\":\"monkey3\","
"      \"name\":\"RELAY-3\""
"    },"
"    \"rmtsw-relay4-00-11-22-33-44-55\": {"
"      \"p\": \"switch\","
"      \"command_topic\":\"relay4/command\","
"      \"state_topic\":\"relay4/state\","      
"      \"unique_id\":\"monkey4\","
"      \"name\":\"RELAY-4\""
"    },"
"    \"rmtsw-relay5-00-11-22-33-44-55\": {"
"      \"p\": \"switch\","
"      \"command_topic\":\"relay5/command\","
"      \"state_topic\":\"relay5/state\","      
"      \"unique_id\":\"monkey5\","
"      \"name\":\"RELAY-5\""
"    },"
"    \"rmtsw-relay6-00-11-22-33-44-55\": {"
"      \"p\": \"switch\","
"      \"command_topic\":\"relay6/command\","
"      \"state_topic\":\"relay6/state\","      
"      \"unique_id\":\"monkey6\","
"      \"name\":\"RELAY-6\""
"    },"
"    \"rmtsw-relay7-00-11-22-33-44-55\": {"
"      \"p\": \"switch\","
"      \"command_topic\":\"relay7/command\","
"      \"state_topic\":\"relay7/state\","      
"      \"unique_id\":\"monkey7\","
"      \"name\":\"RELAY-7\""
"    },"
"    \"rmtsw-relay8-00-11-22-33-44-55\": {"
"      \"p\": \"switch\","
"      \"command_topic\":\"relay8/command\","
"      \"state_topic\":\"relay8/state\","      
"      \"unique_id\":\"monkey8\","
"      \"name\":\"RELAY-8\""
"    }"
"  },"
"  \"qos\": 0"
"}";


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

        //printf("MQTT\n");
        //rmtsw_mqtt_publish(mqtt_client, NULL);
        

        // if ((relay_to_switch >= 0) && (relay_to_switch < 8))
        // {


        //     rmtsw_mqtt_publish_state(relay_to_switch ,mqtt_client, NULL);
        // }
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
                printf("MQTT Error initializing subsystem %d\n", i);
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
int mqtt_initialize_connection(void)
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
        printf("mqtt connect returned %d\n", err);
    }

    connection_initialized = true;

    printf("initialize connection returning %d\n", err);

    return(err);
}

 /*!
 * \brief send home assistant mqtt device discovery
 *
 * \param params none
 * 
 * \return 0 on success
 */
int mqtt_initialize_ha_discovery(void)
{
    int err = -1;

    printf("about to call publish discovery\n");

    rmtsw_mqtt_publish_discovery(mqtt_client, NULL);
    
    

    discovery_initialized = true;

    err = 0;

    printf("initialize discovery returning %d\n", err);
    
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

    if (strlen(topic) == strlen("relay/X/command"))
    {
        if ((strncasecmp(topic, "relay", strlen("relay")) == 0) &&
            (strncasecmp(topic + strlen("relay/X/"), "command", strlen("command")) == 0) &&
            isdigit(topic[strlen("relay/")]))
        {
            relay_to_switch = topic[strlen("relay/")] - '0' - 1;  // switch to zero base
        }
    }
}

// 2. Data Callback: Receives payload chunks
void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) 
{
  if(flags & MQTT_DATA_FLAG_LAST)
    {
        printf("Final message received: %.*s AND relay to switch = %d\n", len, (const char*)data, relay_to_switch);

        if ((relay_to_switch >=0) && (relay_to_switch < config.rmtsw_relay_max))
        {
            if (strncasecmp(data, "ON", 2) == 0)
            {
                web.rmtsw_relay_desired_state[relay_to_switch] = true;
                rmtsw_queue_send((uint8_t)relay_to_switch);
                rmtsw_mqtt_publish_state(relay_to_switch, mqtt_client, NULL);                    
            }
            else if (strncasecmp(data, "OFF", 3) == 0)
            {
                web.rmtsw_relay_desired_state[relay_to_switch] = false;
                rmtsw_queue_send((uint8_t)relay_to_switch);
                rmtsw_mqtt_publish_state(relay_to_switch, mqtt_client, NULL);  
            }
            
            relay_to_switch = -1;
        }
       
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
    int err;

    // Set callbacks
    mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, NULL);

    // Subscribe
    //err = mqtt_subscribe(client, "relay/#/command", 1, mqtt_sub_request_cb, NULL);
    err = mqtt_subscribe(client, "relay/#", 1, mqtt_sub_request_cb, NULL);    
    printf("subscribe result = %d\n", err);
    // SLEEP_MS(2000);
    // err = mqtt_subscribe(client, "relay2/command", 1, mqtt_sub_request_cb, NULL);
    // printf("subscribe result = %d\n", err);
    // SLEEP_MS(2000);
    // err = mqtt_subscribe(client, "relay3/command", 1, mqtt_sub_request_cb, NULL);
    // printf("subscribe result = %d\n", err);
    // SLEEP_MS(2000);    
    // err = mqtt_subscribe(client, "relay4/command", 1, mqtt_sub_request_cb, NULL);
    // printf("subscribe result = %d\n", err);
    // SLEEP_MS(2000);
    // err = mqtt_subscribe(client, "relay5/command", 1, mqtt_sub_request_cb, NULL);
    // printf("subscribe result = %d\n", err);
    // SLEEP_MS(2000);
    // err = mqtt_subscribe(client, "relay6/command", 1, mqtt_sub_request_cb, NULL);
    // printf("subscribe result = %d\n", err);
    // SLEEP_MS(2000);
    // err = mqtt_subscribe(client, "relay7/command", 1, mqtt_sub_request_cb, NULL);
    // printf("subscribe result = %d\n", err);
    // SLEEP_MS(2000);
    // err = mqtt_subscribe(client, "relay8/command", 1, mqtt_sub_request_cb, NULL);
    // printf("subscribe result = %d\n", err);
    // SLEEP_MS(2000);
}

/*PUBLISH**********************************************************************************************/
// 1. Define callback for publish completion
void mqtt_pub_request_cb(void *arg, err_t result) 
{
    if(result != ERR_OK) 
    {
        printf("Publish failed: %d\n", result);
        if (arg) printf("%s\n", arg);
    } else 
    {
        printf("Publish success\n");
    }
}

// 2. Example publish function
void rmtsw_mqtt_publish_discovery(mqtt_client_t *client, void *arg)
{
    const char *pub_payload = "Pico2W Hello!";
    err_t err;
    u8_t qos = 1; // 0, 1, or 2
    u8_t retain = 0;
    static char discovery_payload[1800];
    static char discovery_topic[60];

    printf("Constructing discovery topic\n");
    rmtsw_mqtt_construct_discovery_topic(discovery_topic, sizeof(discovery_topic));
    printf("Topic follows\n%s\n", discovery_topic);    
    printf("Constructing discovery payload\n");
    rmtsw_mqtt_construct_discovery_payload(discovery_payload, sizeof(discovery_payload));
    printf("Payload follows\n%s\n", discovery_payload);
    printf("size of payload = %d\n", strlen(discovery_payload));
    
    //printf("size of payload = %d\n", sizeof(ha_device_discovery_payload));

    //err = mqtt_publish(client, "test/test", pub_payload, strlen(pub_payload), qos, retain, mqtt_pub_request_cb, arg);
   
    // remove existing device from ha
    // retain = 1;
    // err = mqtt_publish(client, "homeassistant/device/rmtsw-00-11-22-33-44-55/config", "", 0, qos, retain, mqtt_pub_request_cb, arg);

    // add device to ha
    retain = 0;
    //err = mqtt_publish(client, "homeassistant/device/rmtsw-00-11-22-33-44-55/config", ha_device_discovery_payload, strlen(ha_device_discovery_payload), qos, retain, mqtt_pub_request_cb, arg);
    err = mqtt_publish(client, discovery_topic, discovery_payload, strlen(discovery_payload), qos, retain, mqtt_pub_request_cb, arg);

    if(err != ERR_OK) 
    {
        printf("Publish discovery error: %d\n", err);
    }
}


void rmtsw_mqtt_publish_state(int relay, mqtt_client_t *client, void *arg)
{
    const char *pub_payload = "Pico2W Hello!";
    err_t err;
    u8_t qos = 1; // 0, 1, or 2
    u8_t retain = 0;
    char state[64];
    char state_payload[8];

    CLIP(relay, 0, 7);

    sprintf(state, "relay/%d/state", relay+1);

    // if (config.rmtsw_relay_default_state[relay])   TODO: this is the proper way!
    if (web.rmtsw_relay_desired_state[relay])
    {
        sprintf(state_payload, "ON");
    }
    else
    {
        sprintf(state_payload, "OFF");
    }

    // send state
    retain = 0;
    err = mqtt_publish(client, state, state_payload, strlen(state_payload), qos, retain, mqtt_pub_request_cb, arg);

    if(err != ERR_OK) 
    {
        printf("Publish state error: %d\n", err);
    }

    printf("published new state. %s = %s\n", state, state_payload);
}

int rmtsw_mqtt_construct_discovery_payload(char *buffer, size_t len)
{
    int err = 0;
    int i; 
    char temp_string[32];

    *buffer = 0;

    STRNCAT(buffer, "{\"dev\": {\"ids\": \"", len);
    sprintf(temp_string, "rs-%02x-%02x-%02x-%02x-%02x-%02x", web.mac[0], web.mac[1], web.mac[2], web.mac[3], web.mac[4], web.mac[5]);
    STRNCAT(buffer, temp_string, len);
    STRNCAT(buffer, "\",\"name\": \"", len); 
    STRNCAT(buffer, config.host_name, len);
    STRNCAT(buffer, "\"},\"o\": {\"name\":\"", len);
    STRNCAT(buffer, config.host_name, len);
    STRNCAT(buffer, "\",\"sw\": \"", len);
    STRNCAT(buffer, PLUTO_VER, len);
    STRNCAT(buffer, "\",\"url\": \"https://github.com/NewmanIsTheStar/remote-switch/wiki\"},\"cmps\": {", len);
    for(i=0; i<config.rmtsw_relay_max; i++)
    {
        STRNCAT(buffer, "\"", len);
        sprintf(temp_string, "rs-r%d-%02x-%02x-%02x-%02x-%02x-%02x", i+1, web.mac[0], web.mac[1], web.mac[2], web.mac[3], web.mac[4], web.mac[5]);
        STRNCAT(buffer, temp_string, len);
        STRNCAT(buffer, "\": {\"p\": \"switch\",\"command_topic\":\"", len);
        sprintf(temp_string, "relay/%d/command", i+1);
        STRNCAT(buffer, temp_string, len);
        STRNCAT(buffer, "\",\"state_topic\":\"", len);
        sprintf(temp_string, "relay/%d/state", i+1);
        STRNCAT(buffer, temp_string, len);
        STRNCAT(buffer, " \",\"unique_id\":\"", len);
        sprintf(temp_string, "rs-id%d-%02x-%02x-%02x-%02x-%02x-%02x", i+1, web.mac[0], web.mac[1], web.mac[2], web.mac[3], web.mac[4], web.mac[5]);
        STRNCAT(buffer, temp_string, len);
        STRNCAT(buffer, "\",\"name\":\"", len);   
        STRNCAT(buffer, config.rmtsw_relay_name[i], len); 
        STRNCAT(buffer, "\"}", len);         
        if (i < (config.rmtsw_relay_max -1))
        {
            STRNCAT(buffer, ",", len);  // home assistant is very fussy about trailing commas
        }
        
    }
    STRNCAT(buffer, "},\"qos\": 0}", len);

    return(err);
}

int rmtsw_mqtt_construct_discovery_topic(char *buffer, size_t len)
{
    int err = 0;
    int i; 
    char temp_string[32];

    *buffer = 0;

    STRNCAT(buffer, "homeassistant/device/rmtsw-", len);
    sprintf(temp_string, "%02x-%02x-%02x-%02x-%02x-%02x", web.mac[0], web.mac[1], web.mac[2], web.mac[3], web.mac[4], web.mac[5]);
    STRNCAT(buffer, temp_string, len);
    STRNCAT(buffer, "/config", len);  
    
    return(0);
}

