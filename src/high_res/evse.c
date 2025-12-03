// SPDX-License-Identifier: GPL-2.0-only
/* EV Charging Example application.
 *
 * Copyright (c) 2008 Jonathan Cameron
 * Copyright (c) 2025 Texas Instruments
 */

#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <linux/types.h>
#include <string.h>
#include <poll.h>
#include <endian.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/iio/buffer.h>
#include <mosquitto.h>
#include <pthread.h>
#include <cjson/cJSON.h>
#include "iio_utils.h"
#include "lv_demo_high_res_private.h"
//#include "../../src/osal/lv_os.h"
//#include "../../src/widgets/label/lv_label.h"


static lv_demo_high_res_api_t * api_hmi;
static struct mosquitto *mosq;
extern lv_obj_t * g_top_label;
extern void update_widget3(void);
char curr_ev_state[20] = "Unplugged";     // Default state is "Unplugged"
int flag_is_mqserv_connected = 0;

/* Callback called when the client receives a CONNACK message from the broker. */
void on_connect_local_pub(struct mosquitto *mosq, void *obj, int reason_code)
{
    /* Print out the connection result. mosquitto_connack_string() produces an
        * appropriate string for MQTT v3.x clients, the equivalent for MQTT v5.0
        * clients is mosquitto_reason_string().
        */
    printf("on_connect: %s\n", mosquitto_connack_string(reason_code));
    if(reason_code != 0){
            /* If the connection fails for any reason, we don't want to keep on
                * retrying in this example, so disconnect. Without this, the client
                * will attempt to reconnect. */
            mosquitto_disconnect(mosq);
    }

    /* You may wish to set a flag here to indicate to your application that the
        * client is now connected. */
}

/* Callback called when the client knows to the best of its abilities that a
 * PUBLISH has been successfully sent. For QoS 0 this means the message has
 * been completely written to the operating system. For QoS 1 this means we
 * have received a PUBACK from the broker. For QoS 2 this means we have
 * received a PUBCOMP from the broker. */
void on_publish_local_pub(struct mosquitto *mosq, void *obj, int mid)
{
    // printf("Message with mid %d has been published.\n", mid);
}

int mqtt_loopback_pub_init(){
  
  int rc;

  /* Required before calling other mosquitto functions */
  mosquitto_lib_init();

  /* Create a new client instance.
   * id = NULL -> ask the broker to generate a client id for us
   * clean session = true -> the broker should remove old sessions when we connect
   * obj = NULL -> we aren't passing any of our private data for callbacks
   */
  mosq = mosquitto_new(NULL, true, NULL);
  if(mosq == NULL){
        fprintf(stderr, "Error: Out of memory.\n");
        return 1;
  }

  /* Configure callbacks. This should be done before connecting ideally. */
  mosquitto_connect_callback_set(mosq, on_connect_local_pub);
  mosquitto_publish_callback_set(mosq, on_publish_local_pub);

  /* Connect to test.mosquitto.org on port 1883, with a keepalive of 60 seconds.
   * This call makes the socket connection only, it does not complete the MQTT
   * CONNECT/CONNACK flow, you should use mosquitto_loop_start() or
   * mosquitto_loop_forever() for processing net traffic. */

  rc = mosquitto_connect(mosq, "127.0.0.1", 1883, 60);
  if(rc != MOSQ_ERR_SUCCESS){
		flag_is_mqserv_connected = 0;
        mosquitto_destroy(mosq);
        fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
        return 1;
  }
  else{
	flag_is_mqserv_connected = 1;
  }

  /* Run the network loop in a background thread, this call returns quickly. */
  rc = mosquitto_loop_start(mosq);
  if(rc != MOSQ_ERR_SUCCESS){
		flag_is_mqserv_connected = 0;
        mosquitto_destroy(mosq);
        fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
        return 1;
  }
  else{
	flag_is_mqserv_connected = 1;
  }

  return 0;
}


void on_connect_local_sub(struct mosquitto *mosq, void *obj, int reason_code)
{
  int rc;
  /* Print out the connection result. mosquitto_connack_string() produces an
   * appropriate string for MQTT v3.x clients, the equivalent for MQTT v5.0
   * clients is mosquitto_reason_string().
   */
  printf("on_connect: %s\n", mosquitto_connack_string(reason_code));
  if(reason_code != 0){
        /* If the connection fails for any reason, we don't want to keep on
         * retrying in this example, so disconnect. Without this, the client
         * will attempt to reconnect. */
        mosquitto_disconnect(mosq);
  }

  /* Making subscriptions in the on_connect() callback means that if the
   * connection drops and is automatically resumed by the client, then the
   * subscriptions will be recreated when the client reconnects. */
  rc = mosquitto_subscribe(mosq, NULL, "everest_api/connector_1/var/session_info", 1);
  if(rc != MOSQ_ERR_SUCCESS){
        fprintf(stderr, "Error subscribing: %s\n", mosquitto_strerror(rc));
        /* We might as well disconnect if we were unable to subscribe */
        mosquitto_disconnect(mosq);
  }
}


/* Callback called when the broker sends a SUBACK in response to a SUBSCRIBE. */
void on_subscribe_local_sub(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
  int i;
  bool have_subscription = false;

  /* In this example we only subscribe to a single topic at once, but a
   * SUBSCRIBE can contain many topics at once, so this is one way to check
   * them all. */
  for(i=0; i<qos_count; i++){
        printf("on_subscribe: %d:granted qos = %d\n", i, granted_qos[i]);
        if(granted_qos[i] <= 2){
                have_subscription = true;
        }
  }
  if(have_subscription == false){
        /* The broker rejected all of our subscriptions, we know we only sent
         * the one SUBSCRIBE, so there is no point remaining connected. */
        fprintf(stderr, "Error: All subscriptions rejected.\n");
        mosquitto_disconnect(mosq);
  }
}

/* Callback called when the client receives a message. */
void on_message_local_sub(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
    // Parse payload as JSON
    cJSON *root = cJSON_Parse((const char *)msg->payload);
    if (!root) {
        fprintf(stderr, "Invalid JSON received: %s\n", (char *)msg->payload);
        return;
    }

    // Find "state" key at first level
    cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (!cJSON_IsString(state) || (state->valuestring == NULL)) {
        fprintf(stderr, "No valid 'state' string in JSON: %s\n", (char *)msg->payload);
        cJSON_Delete(root);
        return;
    }

    strcpy(curr_ev_state, state->valuestring);  // Update global state variable

    lv_lock();
    if (g_top_label != NULL) {                    //if widget has been initialized
        update_widget3();
    }
    lv_unlock();

    cJSON_Delete(root);
}

void mqtt_loopback_sub_init(void * api){
  struct mosquitto *mosq;
  int rc;
  api_hmi = (lv_demo_high_res_api_t*)api;

  /* Required before calling other mosquitto functions */
  mosquitto_lib_init();

  /* Create a new client instance.
   * id = NULL -> ask the broker to generate a client id for us
   * clean session = true -> the broker should remove old sessions when we connect
   * obj = NULL -> we aren't passing any of our private data for callbacks
   */
  mosq = mosquitto_new(NULL, true, NULL);
  if(mosq == NULL){
        fprintf(stderr, "Error: Out of memory.\n");
        return;
  }

  /* Configure callbacks. This should be done before connecting ideally. */
  mosquitto_connect_callback_set(mosq, on_connect_local_sub);
  mosquitto_subscribe_callback_set(mosq, on_subscribe_local_sub);
  mosquitto_message_callback_set(mosq, on_message_local_sub);

  /* Connect to test.mosquitto.org on port 1883, with a keepalive of 60 seconds.
   * This call makes the socket connection only, it does not complete the MQTT
   * CONNECT/CONNACK flow, you should use mosquitto_loop_start() or
   * mosquitto_loop_forever() for processing net traffic. */
  rc = mosquitto_connect(mosq, "127.0.0.1", 1883, 60);
  printf("rc=%d\t MOSQ_ERR_SUCCESS=%d\n", rc, MOSQ_ERR_SUCCESS);
  if(rc != MOSQ_ERR_SUCCESS){
        mosquitto_destroy(mosq);
        fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
        return;
  }

  /* Run the network loop in a blocking call. The only thing we do in this
   * example is to print incoming messages, so a blocking call here is fine.
   *
   * This call will continue forever, carrying automatic reconnections if
   * necessary, until the user calls mosquitto_disconnect().
   */
  mosquitto_loop_forever(mosq, -1, 1);

  mosquitto_lib_cleanup();
  return;
}

/* see evse.h for documentation of the function */
void toggle_ev_charging(int state)
{
    if(!flag_is_mqserv_connected){
        return;
    }
    char payload[20];
    int rc;

    /* Print it to a string for easy human reading - payload format is highly
     * application dependent. */
    snprintf(payload, sizeof(payload), "%s", state ? "Resume charging" : "Pause charging");
                                                                        
    /* Publish the message                  
    * mosq - our client instance                                                 
    * *mid = NULL - we don't want to know what the message id for this message is
    * topic = "example/temperature" - the topic on which this message will be published
    * payloadlen = strlen(payload) - the length of our payload in bytes
    * payload - the actual payload                                    
    * qos = 1 - publish with QoS 1 for this example
    * retain = false - do not use the retained message feature for this message
    */
    rc = mosquitto_publish(mosq, NULL, state ? "everest_api/connector_1/cmd/resume_charging" : 
        "everest_api/connector_1/cmd/pause_charging", strlen(payload), payload, 1, false);
    
    if(rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Error publishing: %s\n", mosquitto_strerror(rc));
    }
}

int *evse_init(lv_demo_high_res_api_t * api)
{
	api_hmi = api;
    mqtt_loopback_pub_init();
    mqtt_loopback_sub_init(api);
	while(1) {
        usleep(500000);
	}
    mosquitto_lib_cleanup();

	return NULL;
}
