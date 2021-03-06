#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <modbus.h>
#include <errno.h>

#include <libbacnet/address.h>
#include <libbacnet/device.h>
#include <libbacnet/handlers.h>
#include <libbacnet/datalink.h>
#include <libbacnet/bvlc.h>
#include <libbacnet/client.h>
#include <libbacnet/txbuf.h>
#include <libbacnet/tsm.h>
#include <libbacnet/ai.h>
#include "bacnet_namespace.h"

#define BACNET_INSTANCE_NO	   80 
#define BACNET_PORT		    0xBAC1
#define BACNET_INTERFACE	    "lo"
#define BACNET_DATALINK_TYPE	    "bvlc"
#define BACNET_SELECT_TIMEOUT_MS    1	    /* ms */

#define MODBUS_ADDRESS	    "140.159.153.159"
#define MODBUS_PORT	    502
#define MODBUS_FIRST_DEVICE  80
#define NUM_LISTS 2

#define RUN_AS_BBMD_CLIENT	    1
#if RUN_AS_BBMD_CLIENT
#define BACNET_BBMD_PORT	    0xBAC0
#define BACNET_BBMD_ADDRESS	    "140.159.160.7"
#define BACNET_BBMD_TTL		    90
#endif



/* global variable to hold contents retreived from linked list */
//uint16_t holding[3];

/* Linked list object (initial structure definition) */
typedef struct s_word_object word_object;
struct s_word_object {
	char *word;
	word_object *next;
};

/* Sets link list to be a series of linked lists */
static word_object *list_heads[NUM_LISTS];

/* setup of theread locking and signaling */
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
//static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;

/*------------------------List related functions------------------------*/
static word_object *list_get_first(word_object **list_head) { //grabs the current header object and assigns the next object in the structure and the new heade
        word_object *first_object;
	first_object = *list_head; // grab the current list header
	*list_head = (*list_head)->next; //set the next element as the list header
	 return first_object; //return the list header obtained above
}

static void add_to_list(word_object **list_head, char *word) {
	word_object *last_object, *tmp_object;
	char *tmp_string=strdup(word);
	tmp_object = malloc(sizeof(word_object)); //create a new tmp_object
	tmp_object->word = tmp_string;
	tmp_object->next = NULL;
	pthread_mutex_lock(&list_lock);
	if (*list_head == NULL) { //initialisation of the first object
		*list_head =tmp_object; //assigns the head of the list pointer to the tetmp object
	} else {
		last_object = *list_head; //set Last object pointer to the start of the structure
		while (last_object->next) { //while the current objects next does not point to NULL
			last_object = last_object->next; //change the last object pointer to the next object
		}
		last_object->next = tmp_object; //set the next pointer to the temp object
		last_object=last_object->next;
	}
	pthread_mutex_unlock(&list_lock);
	pthread_cond_signal(&list_data_ready);
}


/*
static void list_flush(word_object *list_head) {
	pthread_mutex_lock(&list_lock);
	while (list_head != NULL) {
		pthread_cond_signal(&list_data_ready);
		pthread_cond_wait(&list_data_flush, &list_lock);
	}
	pthread_mutex_unlock(&list_lock);
}
*/


/*------------------------Bacnet Functions------------------------*/

/* this function checks for BACNET requests and sends the data from the linked lists*/
static int Update_Analog_Input_Read_Property(BACNET_READ_PROPERTY_DATA *rpdata) {
    word_object *current_object_0;	
    uint16_t holding[3];
    int instance_no = bacnet_Analog_Input_Instance_To_Index(rpdata->object_instance);

    if (rpdata->object_property != bacnet_PROP_PRESENT_VALUE) goto not_pv;
    
    pthread_mutex_lock(&list_lock); 
    
    if(list_heads[instance_no] == NULL){
    	pthread_mutex_unlock(&list_lock);
    	goto not_pv;
    }

  
    current_object_0 = list_get_first(&list_heads[instance_no]);
    holding[instance_no] = strtol(current_object_0->word, NULL, 16);
    free(current_object_0);
    
    pthread_mutex_unlock(&list_lock);
    
    printf("AI_Present_Value request for instance %i. Data:%x\n", instance_no, holding[instance_no]);
    bacnet_Analog_Input_Present_Value_Set(instance_no, holding[instance_no]);
    
not_pv:
 return bacnet_Analog_Input_Read_Property(rpdata);
}

static bacnet_object_functions_t server_objects[] = {
    {bacnet_OBJECT_DEVICE,
	    NULL,
	    bacnet_Device_Count,
	    bacnet_Device_Index_To_Instance,
	    bacnet_Device_Valid_Object_Instance_Number,
	    bacnet_Device_Object_Name,
	    bacnet_Device_Read_Property_Local,
	    bacnet_Device_Write_Property_Local,
	    bacnet_Device_Property_Lists,
	    bacnet_DeviceGetRRInfo,
	    NULL, /* Iterator */
	    NULL, /* Value_Lists */
	    NULL, /* COV */
	    NULL, /* COV Clear */
	    NULL  /* Intrinsic Reporting */
    },
    {bacnet_OBJECT_ANALOG_INPUT,
            bacnet_Analog_Input_Init,
            bacnet_Analog_Input_Count,
            bacnet_Analog_Input_Index_To_Instance,
            bacnet_Analog_Input_Valid_Instance,
            bacnet_Analog_Input_Object_Name,
            Update_Analog_Input_Read_Property,
            bacnet_Analog_Input_Write_Property,
            bacnet_Analog_Input_Property_Lists,
            NULL /* ReadRangeInfo */ ,
            NULL /* Iterator */ ,
            bacnet_Analog_Input_Encode_Value_List,
            bacnet_Analog_Input_Change_Of_Value,
            bacnet_Analog_Input_Change_Of_Value_Clear,
            bacnet_Analog_Input_Intrinsic_Reporting},
    {MAX_BACNET_OBJECT_TYPE}
};

static void register_with_bbmd(void) {
#if RUN_AS_BBMD_CLIENT
    /* Thread safety: Shares data with datalink_send_pdu */
    bacnet_bvlc_register_with_bbmd(
	    bacnet_bip_getaddrbyname(BACNET_BBMD_ADDRESS), 
	    htons(BACNET_BBMD_PORT),
	    BACNET_BBMD_TTL);
#endif
}

static void *minute_tick(void *arg) {
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Expire addresses once the TTL has expired */
	bacnet_address_cache_timer(60);

	/* Re-register with BBMD once BBMD TTL has expired */
	register_with_bbmd();

	/* Update addresses for notification class recipient list 
	 * Requred for INTRINSIC_REPORTING
	 * bacnet_Notification_Class_find_recipient(); */
	
	/* Sleep for 1 minute */
	pthread_mutex_unlock(&timer_lock);
	sleep(60);
    }
    return arg;
}

static void *second_tick(void *arg) {
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Invalidates stale BBMD foreign device table entries */
	bacnet_bvlc_maintenance_timer(1);

	/* Transaction state machine: Responsible for retransmissions and ack
	 * checking for confirmed services */
	bacnet_tsm_timer_milliseconds(1000);

	/* Re-enables communications after DCC_Time_Duration_Seconds
	 * Required for SERVICE_CONFIRMED_DEVICE_COMMUNICATION_CONTROL
	 * bacnet_dcc_timer_seconds(1); */

	/* State machine for load control object
	 * Required for OBJECT_LOAD_CONTROL
	 * bacnet_Load_Control_State_Machine_Handler(); */

	/* Expires any COV subscribers that have finite lifetimes
	 * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
	 * bacnet_handler_cov_timer_seconds(1); */

	/* Monitor Trend Log uLogIntervals and fetch properties
	 * Required for OBJECT_TRENDLOG
	 * bacnet_trend_log_timer(1); */
	
	/* Run [Object_Type]_Intrinsic_Reporting() for all objects in device
	 * Required for INTRINSIC_REPORTING
	 * bacnet_Device_local_reporting(); */
	
	/* Sleep for 1 second */
	pthread_mutex_unlock(&timer_lock);
	sleep(1);
    }
    return arg;
}

static void ms_tick(void) {
    /* Updates change of value COV subscribers.
     * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
     * bacnet_handler_cov_task(); */
}

#define BN_UNC(service, handler) \
    bacnet_apdu_set_unconfirmed_handler(		\
		    SERVICE_UNCONFIRMED_##service,	\
		    bacnet_handler_##handler)
#define BN_CON(service, handler) \
    bacnet_apdu_set_confirmed_handler(			\
		    SERVICE_CONFIRMED_##service,	\
		    bacnet_handler_##handler)
		    
/*------------------------Bridge Communication Functions------------------------*/
/*function reads Modbus Data and adds to linked lists*/
static void *modb(void *arg){
	int i; //incrementing variable 
	int rc; //number of items received from modbus
	uint16_t tab_reg[128]; // storage for data from modbus request
	char sending[64]; //storage value for sending data
	modbus_t *ctx;  //reference to modbus connection
	confailed:;	
	ctx = modbus_new_tcp(MODBUS_ADDRESS, MODBUS_PORT); //connects to remote server
	
	
	/*modbus connection error checking*/
	
	if (ctx == NULL) {
		fprintf(stderr, "Unable to allocate libmodbus context\n");
		sleep(1);
		goto confailed;
	}
	
	if (modbus_connect(ctx) == -1) {
		printf("con failed");
		fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		sleep(1);
		goto confailed;
	}

while (1) {
	rc = modbus_read_registers(ctx, MODBUS_FIRST_DEVICE, NUM_LISTS+1, tab_reg); //reads the data from modbus server 
	
	/* if it failed close connection and then reconnect*/
	if (rc == -1) {
		fprintf(stderr, "%s\n", modbus_strerror(errno));
		modbus_close(ctx);
		modbus_free(ctx);
		usleep(100000);
		goto confailed;
	}
	
	/*add all items received in order to the head of the linked list*/
	for (i=0; i < rc; i++) {
		sprintf(sending, "%x", tab_reg[i]);
		add_to_list(&list_heads[i], sending);
	}
	
	usleep(100000); //wait for 100ms before next read
}
	return arg; //silences compiler warning
}


/*------------------------Function Main------------------------*/
int main(int argc, char **argv) {
	
    uint8_t rx_buf[bacnet_MAX_MPDU];
    uint16_t pdu_len;
    BACNET_ADDRESS src;
    pthread_t modbus_thread_id, minute_tick_id, second_tick_id;

    bacnet_Device_Set_Object_Instance_Number(BACNET_INSTANCE_NO);
    bacnet_address_init();

    /* Setup device objects */
    bacnet_Device_Init(server_objects);
    BN_UNC(WHO_IS, who_is);
    BN_CON(READ_PROPERTY, read_property);

    bacnet_BIP_Debug = true;
    bacnet_bip_set_port(htons(BACNET_PORT));
    bacnet_datalink_set(BACNET_DATALINK_TYPE);
    bacnet_datalink_init(BACNET_INTERFACE);
    atexit(bacnet_datalink_cleanup);
    memset(&src, 0, sizeof(src));

    register_with_bbmd();

    bacnet_Send_I_Am(bacnet_Handler_Transmit_Buffer);
    
    pthread_create(&modbus_thread_id, NULL, modb, NULL);
    pthread_create(&minute_tick_id, 0, minute_tick, NULL);
    pthread_create(&second_tick_id, 0, second_tick, NULL);
    
     

    while (1) {
	pdu_len = bacnet_datalink_receive(
		    &src, rx_buf, bacnet_MAX_MPDU, BACNET_SELECT_TIMEOUT_MS);

	if (pdu_len) {
	    pthread_mutex_lock(&timer_lock);
	    bacnet_npdu_handler(&src, rx_buf, pdu_len);
	    pthread_mutex_unlock(&timer_lock);
	}

	ms_tick();
    }

    return 0;
}
