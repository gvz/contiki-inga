/**
 * \file
 * \brief        A uDTN based distibuted height measurement 
 * \author
 *         Georg von Zengen <vonzengen@ibr.cs.tu-bs.de>
 */

#include "project-conf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contiki.h"
#include "net/netstack.h"
#include "net/packetbuf.h"
#include "watchdog.h"
#include "sys/test.h"
#include "sys/profiling/profiling.h"
#include "node-id.h"

#include "bundle.h"
#include "agent.h"
#include "sdnv.h"
#include "api.h"
#include "storage.h"
#include "discovery.h"
#include "system_clock.h"
#include "bundle_ageing.h"

#include "dt_timesync.h"

#include "button-sensor.h"
#include "pressure-sensor.h"
#include "leds.h"

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#define CLIMBNET_MULTICAST_ID 1
#define CLIMBNET_MULTICAST_SRV_ID 13
#define CLIMBNET_MEASURE_INTERVAL 3

PROCESS(climbnet_process, "Climbnet");
AUTOSTART_PROCESSES(&climbnet_process);



static inline struct mmem *bundle_convenience(uint16_t dest, uint16_t dst_srv, uint16_t src_srv,  uint8_t *data, size_t len)
{
	uint32_t tmp;
	struct mmem *bundlemem;

	bundlemem = bundle_create_bundle();
	if (!bundlemem) {
		PRINTF("create_bundle failed\n");
		return NULL;
	}

	/* Destination node and service */
	tmp=dest;
	bundle_set_attr(bundlemem, DEST_NODE, &tmp);
	tmp=dst_srv;
	bundle_set_attr(bundlemem, DEST_SERV, &tmp);

	/* Source Service */
	tmp=src_srv;
	bundle_set_attr(bundlemem, SRC_SERV, &tmp);

	/* Bundle flags */
	//tmp=BUNDLE_FLAG_SINGLETON;
	//bundle_set_attr(bundlemem, FLAGS, &tmp);

	/* Bundle lifetime */
	tmp=3;
	bundle_set_attr(bundlemem, LIFE_TIME, &tmp);

	/* Bundle payload block */
	bundle_add_block(bundlemem, BUNDLE_BLOCK_TYPE_PAYLOAD, BUNDLE_BLOCK_FLAG_NULL, data, len);

	return bundlemem;
}


PROCESS_THREAD(climbnet_process, ev, data)
{
    static struct etimer timer;
    static struct etimer measurement_timer;
    static struct bundle_block_t *block;

    static const struct sensors_sensor *button_sensor;

    static struct registration_api reg_climb;
    static struct registration_api reg_dummy;
    static uint8_t synced = 0;
    static uint16_t timesync_master;
    struct mmem *bundlemem, *recv;
    typedef struct {
        int16_t temperature;
        int32_t pressure;
    } height_t;
    typedef struct {
        height_t height; 
	    uint32_t time;
        uint16_t node_id;
    } climbnet_payload_t;
    static climbnet_payload_t climbnet_payload_a[80 / sizeof(climbnet_payload_t)]; //give me an array that fills one bundle without fragmentation
    static uint16_t climbnet_payload_pointer = 0;

    static uint8_t i = 0;

    PROCESS_BEGIN();
	leds_off(LEDS_GREEN | LEDS_YELLOW); 
    etimer_set(&timer, CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer) );
    timesync_init();
    
    /* Register our endpoint */
    reg_climb.status = APP_ACTIVE;
    reg_climb.application_process = PROCESS_CURRENT();
    reg_climb.app_id = CLIMBNET_MULTICAST_SRV_ID;
    reg_climb.node_id = CLIMBNET_MULTICAST_ID;
    process_post(&agent_process, dtn_application_registration_event, &reg_climb);
    reg_dummy.status = APP_ACTIVE;
    reg_dummy.application_process = PROCESS_CURRENT();
    reg_dummy.app_id = CLIMBNET_MULTICAST_SRV_ID;
    reg_dummy.node_id = dtn_node_id;
    process_post(&agent_process, dtn_application_registration_event, &reg_dummy);
    PRINTF("started climbnet process\n");

    /* Hardware init*/
    // get pointer to sensor
    button_sensor = sensors_find("Button");

    // activate and check status
    uint8_t status = SENSORS_ACTIVATE(*button_sensor);
    if (status == 0) {
        printf("Error: Failed to init button sensor, aborting...\n");
    }

    // get pointer to sensor (combined pressure and temperature sensor)
    static const struct sensors_sensor *temppress_sensor;
    temppress_sensor = sensors_find("Press");

    // activate and check status
    status = SENSORS_ACTIVATE(*temppress_sensor);
    if (status == 0) {
        printf("Error: Failed to init pressure sensor, aborting...\n");
        leds_off(LEDS_GREEN | LEDS_YELLOW); 
        PROCESS_EXIT();
    }

    etimer_set(&timer, CLOCK_SECOND);
    while (1) {
        PROCESS_YIELD();
        if (ev == sensors_event && data == button_sensor) {
            if (button_sensor->value(0)){
                timesync_activate_pairing();
                leds_on( LEDS_YELLOW); 
            } else {
                timesync_deactivate_pairing();
                leds_off( LEDS_YELLOW); 
            }
        }
        if (etimer_expired(&timer)){ 
            etimer_set(&timer, CLOCK_SECOND);
            if (udtn_getclockstate() > UDTN_CLOCK_STATE_UNKNOWN){
	            leds_on(LEDS_GREEN);
	            udtn_timeval_t date;
	            udtn_gettimeofday(&date); 
	            uint32_t tmpsec = date.tv_sec % CLIMBNET_MEASURE_INTERVAL;
	            uint32_t tmpusec = date.tv_usec / (1000000/CLOCK_SECOND);
	            etimer_set(&measurement_timer, (tmpsec * CLOCK_SECOND)  - tmpusec);
	            //PRINTF("CLIMBNET: paired %u \n", CLOCK_SECOND);
            }
        }
        if (etimer_expired(&measurement_timer) && udtn_getclockstate() > UDTN_CLOCK_STATE_UNKNOWN){ 
            udtn_timeval_t date;
            udtn_gettimeofday(&date); 
            uint32_t tmp = date.tv_sec % CLIMBNET_MEASURE_INTERVAL;
            uint32_t tmpusec = date.tv_usec / (1000000/CLOCK_SECOND);
            if (tmp == 0){
	            tmp = CLIMBNET_MEASURE_INTERVAL;
            }
            etimer_set(&measurement_timer, (tmp * CLOCK_SECOND) - tmpusec);
            PRINTF("CLIMBNET: measure\n");
            // get temperature value
            int16_t tempval = temppress_sensor->value(TEMP);
            // get 32bit pressure value
            int32_t pressval = ((int32_t) temppress_sensor->value(PRESS_H) << 16);
            pressval |= (temppress_sensor->value(PRESS_L) & 0xFFFF);
            // read and output values
            PRINTF("CLIMBNET: %d, press: %ld time: %lu\n", tempval, pressval, date.tv_sec);

            // save measurement in array 
            climbnet_payload_a[climbnet_payload_pointer].time = date.tv_sec;
            climbnet_payload_a[climbnet_payload_pointer].height.pressure = pressval;
            climbnet_payload_a[climbnet_payload_pointer].height.temperature = tempval;
            climbnet_payload_a[climbnet_payload_pointer].node_id = dtn_node_id;
            climbnet_payload_pointer++;
            if (climbnet_payload_pointer >= 80 / sizeof(climbnet_payload_t)){
	            climbnet_payload_pointer = 0;
            }

        }
    
    }

  PROCESS_END();
}
