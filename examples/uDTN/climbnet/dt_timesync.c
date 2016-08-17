/*
 * Copyright (c) 2012, Georg von Zengen <vonzengen@ibr.cs.tu-bs.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         A uDTN time synchronization
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

#define MODE_PASSIVE 0
#define MODE_ACTIVE 1
#define MODE_LOOPBACK 2



#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#ifdef CONF_PING_TIMEOUT
#define PING_TIMEOUT CONF_PING_TIMEOUT
#else
#define PING_TIMEOUT 10
#endif
/*---------------------------------------------------------------------------*/
PROCESS(timesync_process, "Timesync");

AUTOSTART_PROCESSES(&timesync_process);

static uint8_t pairing_active = 1;
/*---------------------------------------------------------------------------*/

static clock_time_t get_time()
{
	return clock_time();
}

/* Convenience function to populate a bundle */
static inline struct mmem *bundle_convenience(uint16_t dest, uint16_t dst_srv, uint16_t src_srv,  uint8_t *data, size_t len)
{
	uint32_t tmp;
	struct mmem *bundlemem;

	bundlemem = bundle_create_bundle();
	if (!bundlemem) {
		printf("create_bundle failed\n");
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

void timesync_init()
{
	printf("Starting timesync\n");

	process_start(&timesync_process, NULL);
}
void timesync_activate_pairing(){
	pairing_active = 1;
}
void timesync_deactivate_pairing(){
	pairing_active = 0;
}

PROCESS_THREAD(timesync_process, ev, data)
{
	static struct etimer timer;
	struct bundle_block_t *block;


	#define MAX_SYNC_GROUP_MEMBERS 10
	static uint16_t timesync_group[MAX_SYNC_GROUP_MEMBERS]; 
	static struct registration_api reg_sync;
	static struct registration_api reg_dummy;
	static uint8_t synced = 0;
	static uint16_t timesync_master;
	struct mmem *bundlemem, *recv;
	struct time_sync_payload_t{
		udtn_clock_state_t state;
		udtn_timeval_t time;
		uint16_t node_id;
	} ;
	static struct time_sync_payload_t time_sync_payload;


	static uint32_t * timestamp;
	static uint32_t * sequence;
	static uint8_t sent = 0;
	static uint8_t i = 0;

	PROCESS_BEGIN();
	timesync_master = dtn_node_id;
	for (; i<MAX_SYNC_GROUP_MEMBERS; i++){
		timesync_group[i]=0;
	}
	etimer_set(&timer, CLOCK_SECOND);
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer) );
	udtn_clock_init();
    
	     	/* Register our endpoint */
	reg_sync.status = APP_ACTIVE;
	reg_sync.application_process = PROCESS_CURRENT();
	reg_sync.app_id = 12;
	reg_sync.node_id = 1;
	process_post(&agent_process, dtn_application_registration_event, &reg_sync);
	reg_dummy.status = APP_ACTIVE;
	reg_dummy.application_process = PROCESS_CURRENT();
	reg_dummy.app_id = 12;
	reg_dummy.node_id = dtn_node_id;
	process_post(&agent_process, dtn_application_registration_event, &reg_dummy);
	printf("started timesync process\n");


	/* Transfer */
	while(1) {
		etimer_set(&timer, CLOCK_SECOND);
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer) ||
				ev == submit_data_to_application_event);
        clock_time_t tmp_sec = clock_seconds();

        if (etimer_expired(&timer)) {

			//send time sync beacon if this node is master or if there is no group now
			if (timesync_master == dtn_node_id){
				time_sync_payload.state = udtn_getclockstate();
				udtn_gettimeofday(&time_sync_payload.time);
				time_sync_payload.node_id = dtn_node_id;
				bundlemem = bundle_convenience(1, 12, 12, (uint8_t*)&time_sync_payload, sizeof(struct time_sync_payload_t));
                if (bundlemem) {
	                process_post(&agent_process, dtn_send_bundle_event, (void *) bundlemem);
	                printf("TIME_SYNC: sending sync\n");
                } else {
                    printf("TIME_SYNC: unable to send sync\n");
                }
			}

			udtn_timeval_t time;
			udtn_gettimeofday(&time);
			printf("TIME: %lu %lu\n",time.tv_sec,time.tv_usec);
		}

		if( ev == submit_data_to_application_event ) {
            printf("TIME_SYNC: i got a bundle\n");
			/* We received a bundle - handle it */
			recv = (struct mmem *) data;
			/* Check receiver */
			block = bundle_get_payload_block(recv);
			if( block == NULL ) {
				printf("TIME_SYNC: No Payload\n");
			} else {
				struct time_sync_payload_t *tmp_load = (struct time_sync_payload_t*)block->payload; 
				if (tmp_load->node_id == dtn_node_id){
					//it my bundle so I do not care
					printf("TIME_SYNC: %u received my own packet, drop it\n", tmp_load->node_id);
					process_post(&agent_process, dtn_processing_finished, recv);
					continue;
                }
				printf("TIME_SYNC: received budle from %u\n",tmp_load->node_id);

				// check if this node is kown
                i = 0;
                uint8_t known = 0;
                uint8_t next = 0;
                for (; i<MAX_SYNC_GROUP_MEMBERS; i++){
	                if (timesync_group[i] == tmp_load->node_id){
		                known = 1;
		                break;
	                }
	                if (timesync_group[i] == 0){
		                next = i;
		                break;
	                }
				}
                if (!known && pairing_active){
	                //add node to timesync group if pairing mode is active
	                printf("TIME_SYNC: adding %u to timesync group\n",tmp_load->node_id);
	                timesync_group[next] = tmp_load->node_id;
	                if ( (tmp_load->node_id < timesync_master && tmp_load->state >= udtn_getclockstate()) ||
	                     // if the node_id is smaller and the cock is equal of more accurate as ours 
	                     tmp_load->state > udtn_getclockstate() ){
		                // or if the its clock is more accurate we use it as the clock master

		                timesync_master = tmp_load->node_id;
                        printf("TIME_SYNC: %u is our master \n",tmp_load->node_id);
	                }
                }
                if (tmp_load->node_id == timesync_master){
	                // this is a time sync packet form our time master, so we need to set our clock
                    printf("TIME_SYNC: got bundel from our master %u\n",tmp_load->node_id);
                    printf("TIME_SYNC: bundel is %lu ms old\n", bundle_ageing_get_age(recv));
                    uint32_t tmp_age = bundle_ageing_get_age(recv);
                    udtn_timeval_t tmp_val;
                    udtn_timeval_t set_timeval;
                    tmp_val.tv_sec = tmp_age / 1000;
                    tmp_val.tv_usec = (tmp_age - tmp_val.tv_sec) * 1000;
                    udtn_timeradd(&tmp_load->time, &tmp_val, &set_timeval);
                    udtn_settimeofday(&set_timeval);
                }
			}

			// Tell the agent, that we have processed the bundle
			process_post(&agent_process, dtn_processing_finished, recv);

		}
	}


	PROCESS_END();
}
/*---------------------------------------------------------------------------*/

