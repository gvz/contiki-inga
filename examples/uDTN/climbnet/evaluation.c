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
#include "fat/diskio.h"
#include "fat/cfs-fat.h"

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
    static struct diskio_device_info *info = 0;
    static struct FAT_Info fat;
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
    static char filename[] = "cn0001.csv";
    static char message[100];

    PROCESS_BEGIN();
	leds_off(LEDS_GREEN | LEDS_YELLOW); 
    etimer_set(&timer, CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer) );
    timesync_init();
    timesync_activate_pairing();
    leds_on( LEDS_YELLOW); 

    PCMSK0 |= (1 << PCINT0);
    PCICR |= (1 << PCIE0);
    
    etimer_set(&timer, CLOCK_SECOND);
    while (1) {
        PROCESS_YIELD();
        etimer_set(&timer, CLOCK_SECOND);
        if (udtn_getclockstate() > UDTN_CLOCK_STATE_UNKNOWN){
            leds_on(LEDS_GREEN);
            leds_off( LEDS_YELLOW); 
	        timesync_deactivate_pairing();
        }
        else{
	        timesync_activate_pairing();
        }
    }

  PROCESS_END();
}


ISR(PCINT0_vect){
	udtn_timeval_t time;
    udtn_gettimeofday(&time);
    printf("%lu %lu\n",time.tv_sec,time.tv_usec);
}
