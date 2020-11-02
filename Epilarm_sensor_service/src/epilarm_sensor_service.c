#include <tizen.h> // standard header from the template
#include <service_app.h> // standard header from the template
#include "epilarm_sensor_service.h"

// headers that will be needed for our service:
//stdlib...
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <dirent.h>
//tizen...
#include <sensor.h>
#include <device/power.h>
#include <notification.h>

//mqtt lib
#include <mqtt.h>
#include <posix_sockets.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>


//own modules
#include <fft.h>
#include <rb.h>





// some constant values used in the app
#define MYSERVICELAUNCHER_APP_ID "QOeM6aBGp0.Epilarm" // an ID of the UI application of our package
#define STRNCMP_LIMIT 256 // the limit of characters to be compared using strncmp function

#define sampleRate 50 //in Hz (no samples per sec), this leads an interval of 1/sampleRate secs between measurements
#define dataQueryInterval 20 //time in ms between measurements //only possible to do with 20, 40, 60, 80, 100, 200, 300 (results from experiments)
#define dataAnalysisInterval 10 //time in s that are considered for the FFT

//we have a sample frequency of sampleRate = 50Hz and for each FFT we consider dataAnalysisInterval = 10s of time,
//i.e., we have a frequency resolution of 1/dataAnalysisInterval = 1/10s = 0.1Hz (should be considerably smaller than 0.5Hz)
//the maximal freq we can detect with FFT is sampleRate/2 = 25Hz (and should be larger than 10Hz, so large enough! -> the bigger the better!)
//design idea:
// (1) an alarm is raised in two steps: the first is a WARNING step, this is raised if the movements are likely to correspond to a seizure
//                                      the second is an ALARM step, this is raised after being in WARNING state for a specified amount of secs
// (2) to get into WARNING mode, two conditions have to be satisfied: first the freq corresponding to seizure-like movements are present and above
//                                                                    a certain threshold, second those 'seizure'-freq are on average considerably
//        															  larger than some fixed multiple of the freqs of all other freqs.

//buffers for the three linear acceleration measurements
int bufferSize = sampleRate*dataAnalysisInterval; //size of stored data on which we perform the FFT


// mqtt connection info
struct mqtt_state_t {
    const char* hostname;
    const char* port;
    const char* topic;
    const char* username;
    const char* password;
    const char* client_id;
    uint8_t* sendbuf;
    size_t sendbufsz;
    uint8_t* recvbuf;
    size_t recvbufsz;
};


// application data (context) that will be passed to functions when needed
typedef struct appdata
{
	sensor_h sensor; // sensor handle
	sensor_listener_h listener; // sensor listener handle

	//params of analysis (set by UI on startup)
	double minFreq;
	double maxFreq;
	double avgRoiThresh;
	double avg_roi_x, avg_nroi_x;
	double avg_roi_y, avg_nroi_y;
	double avg_roi_z, avg_nroi_z;
	double multThresh;
	int warnTime;
	double multRatio;
	double avgRoi;
	//logging of data AND sending over mqtt to broker
	bool logging;
	struct mqtt_client client;
	struct mqtt_state_t mqtt_state; //state info on mqtt connection
	pthread_t mqtt_daemon;


    //indicate if analysis and sensor listener are running... (required to give appropriate debugging output when receiving appcontrol)
    bool running;

	//current alarmState
    int alarmState;

	//circular buffers for x, y, and z linear acc data
    ringbuf_t rb_x;
    ringbuf_t rb_y;
    ringbuf_t rb_z;

	FFTTransformer* fft_x;
	FFTTransformer* fft_y;
	FFTTransformer* fft_z;

	//store results of ffts of buffers
    double* fft_x_spec;
    double* fft_y_spec;
    double* fft_z_spec;

    //simplify fft_X_spec s.t. each entry collects the sum of magnitudes of 0.5Hz width (so fft_X_spec_simplified[0] ~ 0.1-0.5Hz, [1] ~ 0.6-1Hz, ..., [9] ~ 4.6-5Hz)
    double* fft_x_spec_simplified;
    double* fft_y_spec_simplified;
    double* fft_z_spec_simplified;
} appdata_s;


//locally save analyzed data (called after each analysis and stored locally) -> CAUTION: must not be called more than once per second!
void save_log(void *data) {
	// Extracting application data
	appdata_s* ad = (appdata_s*)data;

	//get timestamp:
	struct timespec tmnow;
	struct tm *tm;
	char timebuf[30], nsec_buf[6];

	clock_gettime(CLOCK_REALTIME, &tmnow);
	tm = localtime(&tmnow.tv_sec);
	strftime(timebuf, 30, "%Y-%m-%d_%H-%M-%S-", tm);
	sprintf(nsec_buf, "%d", (int) round(tmnow.tv_nsec/1000000));
	strcat(timebuf, nsec_buf);

	//find appropriate file name and path
	char file_path[256];
	char* data_path = app_get_data_path();
	snprintf(file_path, sizeof(file_path), "%slog_%s.%s", data_path, timebuf, "json");
	free(data_path);

	dlog_print(DLOG_INFO, LOG_TAG, "save_log: data_path=%s", file_path);

	//print contents in json-format using json-glib
	JsonBuilder *builder = json_builder_new ();

	json_builder_begin_object (builder);
	//add timestamp
	json_builder_set_member_name (builder, "time");
	json_builder_add_string_value (builder, timebuf);

	//add alarmstate
	json_builder_set_member_name (builder, "alarmstate");
	json_builder_add_int_value (builder, ad->alarmState);

	//add multRatio
	json_builder_set_member_name (builder, "multRatio");
	json_builder_add_double_value (builder, ad->multRatio);

	//add avgRoi
	json_builder_set_member_name (builder, "avgRoi");
	json_builder_add_double_value (builder, ad->avgRoi);

	//add avg_roi_x|y|z
	json_builder_set_member_name (builder, "avg_roi");
	json_builder_begin_array(builder);
	json_builder_add_double_value(builder, ad->avg_roi_x);
	json_builder_add_double_value(builder, ad->avg_roi_y);
	json_builder_add_double_value(builder, ad->avg_roi_z);
	json_builder_end_array(builder);

	//add avg_nroi_x|y|z
	json_builder_set_member_name (builder, "avg_nroi");
	json_builder_begin_array(builder);
	json_builder_add_double_value(builder, ad->avg_nroi_x);
	json_builder_add_double_value(builder, ad->avg_nroi_y);
	json_builder_add_double_value(builder, ad->avg_nroi_z);
	json_builder_end_array(builder);

	//include the bins of fft_x_spec_simplified corresponding to the first 20 bins (so in the 0-10Hz range)
	//json_builder_set_member_name (builder, "x_spec");
	//json_builder_begin_array(builder);
	//for (int i = 0; i < 20; ++i) {
	//	json_builder_add_double_value(builder, ad->fft_x_spec_simplified[i]);
	//}
	//json_builder_end_array(builder);
	//
	//include the bins of fft_y_spec_simplified corresponding to the first 40 bins (so in the 0-20Hz range)
	//json_builder_set_member_name (builder, "y_spec");
	//json_builder_begin_array(builder);
	//for (int i = 0; i < 20; ++i) {
	//	json_builder_add_double_value(builder, ad->fft_y_spec_simplified[i]);
	//}
	//json_builder_end_array(builder);
	//
	//include the bins of fft_z_spec_simplified corresponding to the first 40 bins (so in the 0-20Hz range)
	//json_builder_set_member_name (builder, "z_spec");
	//json_builder_begin_array(builder);
	//for (int i = 0; i < 20; ++i) {
	//	json_builder_add_double_value(builder, ad->fft_z_spec_simplified[i]);
	//}
	//json_builder_end_array(builder);


	//include params of analysis (in one array)
	json_builder_set_member_name (builder, "params");
	json_builder_begin_array(builder);
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "minFreq");
	json_builder_add_double_value(builder, ad->minFreq);
	json_builder_set_member_name(builder, "maxFreq");
	json_builder_add_double_value(builder, ad->maxFreq);
	json_builder_set_member_name(builder, "avgRoiThresh");
	json_builder_add_double_value(builder, ad->avgRoiThresh);
	json_builder_set_member_name(builder, "multThresh");
	json_builder_add_double_value(builder, ad->multThresh);
	json_builder_set_member_name(builder, "warnTime");
	json_builder_add_int_value(builder, ad->warnTime);
	json_builder_end_object(builder);
	json_builder_end_array(builder);

	json_builder_end_object (builder);

	//generate json string (will be in str)
	JsonGenerator *gen = json_generator_new();
	JsonNode * root = json_builder_get_root(builder);
	json_generator_set_root(gen, root);
	gchar *str = json_generator_to_data(gen, NULL);

	json_node_free(root);
	g_object_unref(gen);
	g_object_unref(builder);

	dlog_print(DLOG_INFO, LOG_TAG, "save_log: content with len %d is = '%s'", strlen(str), str);


	//write contents to file
    FILE *fp;
    fp = fopen(file_path, "w");
    fputs(str, fp);
    fclose(fp);
}

char* read_file(const char* filename)
{
	char * buffer = 0;
	long length;
	FILE * f = fopen(filename, "r");
	if (f==NULL){dlog_print(DLOG_INFO, LOG_TAG, "fopen did NOT succeed! (f=NULL)");}


	if (f)
	{
	  fseek(f, 0, SEEK_END);
	  length = ftell(f);
	  fseek(f, 0, SEEK_SET);
	  buffer = malloc(length);
	  if (buffer)
	  {
	    fread(buffer, 1, length, f);
	  }
	  fclose(f);
	}
	return buffer;
}

void publish_callback(void** unused, struct mqtt_response_publish *published)
{
    /* not used in our setting (we do not subscribe to any topic...) */
}

void share_data(void* data) {
	// Extracting application data
	appdata_s* ad = (appdata_s*)data;

	//share data
	dlog_print(DLOG_INFO, LOG_TAG_MQTT, "initializing publishing of locally stored json-files... (sending starting message...)");
	mqtt_publish(&ad->client, ad->mqtt_state.topic, "1", 1, MQTT_PUBLISH_QOS_1);
	mqtt_sync(&ad->client);

    //send all locally saved files -- ony-by-one --
	char* data_msg; //this seems to be large enough
	char file_path[256];
	char* data_path = app_get_data_path();

    //iterate over files:
    DIR *d;
    struct dirent *dir;
    d = opendir(data_path);
    if (d) {
    	while ((dir = readdir(d)) != NULL && strlen(dir->d_name)>5) {
    		//now dir->d_name stores name of file in dir
    		if (strncmp(dir->d_name,"log_",4) == 0 && strcmp(strrchr(dir->d_name, '.'), ".json") == 0)
    		{
    			dlog_print(DLOG_INFO, LOG_TAG_MQTT, "processing file %s", dir->d_name);
    			//set correct file_path
    			snprintf(file_path, sizeof(file_path), "%s%s", data_path, dir->d_name);

    			dlog_print(DLOG_INFO, LOG_TAG_MQTT, "reading %s", file_path);
    			/* print a message */
    			data_msg = read_file(file_path);
    			dlog_print(DLOG_INFO, LOG_TAG_MQTT, "reading done! (%s)", data_msg);

    			dlog_print(DLOG_INFO, LOG_TAG_MQTT, "publishing msg: topic='%s', payload_len=%d, payload='%s'.", ad->mqtt_state.topic, strlen(data_msg)+1, data_msg);

    			/* publish the test message */
    			mqtt_publish(&ad->client, ad->mqtt_state.topic, data_msg, strlen(data_msg) + 1, MQTT_PUBLISH_QOS_1);
    			mqtt_sync(&ad->client);
    			free(data_msg);
    			dlog_print(DLOG_INFO, LOG_TAG_MQTT, "message published!");

    			/* check for errors */
    			if (ad->client.error != MQTT_OK) {
    				dlog_print(DLOG_INFO, LOG_TAG_MQTT, "error: %s", mqtt_error_str(ad->client.error));
    				return;
    			} else { //no error occured, file can be removed
    				dlog_print(DLOG_INFO, LOG_TAG_MQTT, "file sent successfully: %s", mqtt_error_str(ad->client.error));
    				remove(dir->d_name);
    				dlog_print(DLOG_INFO, LOG_TAG_MQTT, "local file deleted.");
    			}
    		} else {
    			//dir is either no regular file, its name is shorter than 5 chars or it does not end with .json
    			dlog_print(DLOG_INFO, LOG_TAG_MQTT, "skipping 'file' %s", dir->d_name);
    		}
    	}
    	closedir(d);
	} else {
		dlog_print(DLOG_INFO, LOG_TAG_MQTT, "directory does not exist!");
	}
	free(data_path);

	dlog_print(DLOG_INFO, LOG_TAG_MQTT, "(sending ending message...)");
	mqtt_publish(&ad->client, ad->mqtt_state.topic, "0", 1, MQTT_PUBLISH_QOS_1);
	mqtt_sync(&ad->client);
}

void* share_data_daemon(void* data)
{
	// Extracting application data
	appdata_s* ad = (appdata_s*) data;

	dlog_print(DLOG_INFO, LOG_TAG_MQTT, "started share_data_daemon");

    while(1)
    {
    	mqtt_sync(&ad->client);
        share_data(ad);
    	mqtt_sync(&ad->client);
        //sleep(10); //send data every 10 secs
    	usleep(500000U); //every 0.5s
    }
    return NULL;
}

//send all locally saved data over mqtt to broker if available and delete files after successful publication (QoS 1)
void reconnect_client(struct mqtt_client* client, void **reconnect_state_vptr) {
    struct mqtt_state_t *mqtt_state = *((struct mqtt_state_t**) reconnect_state_vptr);
    //struct reconnect_state_t *reconnect_state = *((struct reconnect_state_t**) reconnect_state_vptr);
	dlog_print(DLOG_INFO, LOG_TAG_MQTT, "mqtt reconnect started...");

    /* Close the clients socket if this isn't the initial reconnect call */
    if (client->error != MQTT_ERROR_INITIAL_RECONNECT) {
        close(client->socketfd);
    }

    /* Perform error handling here. */
    if (client->error != MQTT_ERROR_INITIAL_RECONNECT) {
        dlog_print(DLOG_INFO, LOG_TAG_MQTT, "reconnect_client: called while client was in error state '%s'", mqtt_error_str(client->error));
        //TODO handle specific errors!
    }

    /* Open a new socket. */
    int sockfd = open_nb_socket(mqtt_state->hostname, mqtt_state->port);
    if (sockfd == -1) {
 	    dlog_print(DLOG_INFO, LOG_TAG_MQTT, "Failed to open socket!");
    } else {
        dlog_print(DLOG_INFO, LOG_TAG_MQTT, "Opened socket! (%d)", sockfd);
 	}

    /* Reinitialize the client. */
    mqtt_reinit(client, sockfd, mqtt_state->sendbuf, mqtt_state->sendbufsz, mqtt_state->recvbuf, mqtt_state->recvbufsz);

    /* Ensure we have a clean session */
    uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
    /* Send connection request to the broker. */
	mqtt_connect(client, mqtt_state->client_id, NULL, NULL, 0, mqtt_state->username, mqtt_state->password, connect_flags, 400);
	dlog_print(DLOG_INFO, LOG_TAG_MQTT, "mqtt reconnect finished!");
}

//starts daemon for continuous sharing of all locally saved files (trying to send every 10mins)
void start_mqtt_daemon(void* data) {
	// Extracting application data
	appdata_s* ad = (appdata_s*) data;

	//make sure connection is reconnected when needed.
	mqtt_init_reconnect(&ad->client, reconnect_client, &ad->mqtt_state, publish_callback);

	dlog_print(DLOG_INFO, LOG_TAG_MQTT, "started mqtt_init_reconnect");

	/* start a thread to share data every 10mins (and update client) */
    if(pthread_create(&ad->mqtt_daemon, NULL, share_data_daemon, data)) {
        fprintf(stderr, "Failed to start client daemon.\n");
    }
}

void stop_mqtt_daemon(void* data) {
	// Extracting application data
	appdata_s* ad = (appdata_s*) data;

	/* disconnect */
	mqtt_disconnect(&ad->client);

	/* close socket and cancel data-sharing daemon */
    close(ad->client.socketfd);
    pthread_cancel(ad->mqtt_daemon);
	dlog_print(DLOG_INFO, LOG_TAG_MQTT, "service disconnected from %s, socket closed and data-sharing daemon cancelled.", ad->mqtt_state.hostname);
}

//send notification
void issue_warning_notification(int as)
{
	dlog_print(DLOG_ERROR, LOG_TAG, "creating notification!");

	notification_h warn_notification = NULL;
	warn_notification = notification_create(NOTIFICATION_TYPE_NOTI);
	if (warn_notification == NULL) {
		dlog_print(DLOG_ERROR, LOG_TAG, "could not create warning notification!");
		return;
	}

	int noti_err = NOTIFICATION_ERROR_NONE;
	noti_err = notification_set_text(warn_notification, NOTIFICATION_TEXT_TYPE_TITLE, "WARNING!", "EPILARM_NOTIFICATION_TITLE", NOTIFICATION_VARIABLE_TYPE_NONE);
	if (noti_err != NOTIFICATION_ERROR_NONE) {
		dlog_print(DLOG_ERROR, LOG_TAG, "could not create title of warning notification!");
		return;
	}
	dlog_print(DLOG_ERROR, LOG_TAG, "notification title set!");

	noti_err = notification_set_text(warn_notification, NOTIFICATION_TEXT_TYPE_CONTENT, "Seizure-like movements detected! (alarmState = %d)",
			"EPILARM_NOTIFICATION_CONTENT", NOTIFICATION_VARIABLE_TYPE_INT, as, NOTIFICATION_VARIABLE_TYPE_NONE);
	if (noti_err != NOTIFICATION_ERROR_NONE) {
		dlog_print(DLOG_ERROR, LOG_TAG, "could not create content of warning notification!");
		return;
	}
	dlog_print(DLOG_ERROR, LOG_TAG, "notification content set!");

	noti_err = notification_set_vibration(warn_notification, NOTIFICATION_VIBRATION_TYPE_DEFAULT, NULL);
	if (noti_err != NOTIFICATION_ERROR_NONE) {
		dlog_print(DLOG_ERROR, LOG_TAG, "could not set vibration of warning notification!");
	    return;
	}
	dlog_print(DLOG_ERROR, LOG_TAG, "notification vibration set!");


	noti_err = notification_post(warn_notification);
	if (noti_err != NOTIFICATION_ERROR_NONE) {
		dlog_print(DLOG_ERROR, LOG_TAG, "could not post warning notification!");
	    return;
	}
	dlog_print(DLOG_ERROR, LOG_TAG, "notification posted!");
}

//send notification
void issue_alarm_notification(int as)
{

	notification_h warn_notification = NULL;
	warn_notification = notification_create(NOTIFICATION_TYPE_NOTI);
	if (warn_notification == NULL) { dlog_print(DLOG_ERROR, LOG_TAG, "could not create warning notification!"); return;}

	int noti_err = NOTIFICATION_ERROR_NONE;
	noti_err = notification_set_text(warn_notification, NOTIFICATION_TEXT_TYPE_TITLE, "ALARM!", "EPILARM_NOTIFICATION_TITLE", NOTIFICATION_VARIABLE_TYPE_NONE);
	if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_ERROR, LOG_TAG, "could not create title of warning notification!"); return; }

	noti_err = notification_set_text(warn_notification, NOTIFICATION_TEXT_TYPE_CONTENT, "Seizure-like movements detected! (alarmState = %d)",
			"EPILARM_NOTIFICATION_CONTENT", NOTIFICATION_VARIABLE_TYPE_INT, as, NOTIFICATION_VARIABLE_TYPE_NONE);
	if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_ERROR, LOG_TAG, "could not create content of warning notification!"); return; }

	noti_err = notification_set_vibration(warn_notification, NOTIFICATION_VIBRATION_TYPE_DEFAULT, NULL);
	if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_ERROR, LOG_TAG, "could not set vibration of warning notification!"); return; }

	noti_err = notification_post(warn_notification);
	if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_ERROR, LOG_TAG, "could not post warning notification!"); return; }
}

//send notification
void issue_unplanned_shutdown_notification()
{
	notification_h warn_notification = NULL;
	warn_notification = notification_create(NOTIFICATION_TYPE_NOTI);
	if (warn_notification == NULL) { dlog_print(DLOG_ERROR, LOG_TAG, "could not create shutdown notification!"); return;}

	int noti_err = NOTIFICATION_ERROR_NONE;
	noti_err = notification_set_text(warn_notification, NOTIFICATION_TEXT_TYPE_TITLE, "Warning!", "EPILARM_NOTIFICATION_TITLE", NOTIFICATION_VARIABLE_TYPE_NONE);
	if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_ERROR, LOG_TAG, "could not create title of shutdown notification!"); return; }

	noti_err = notification_set_text(warn_notification, NOTIFICATION_TEXT_TYPE_CONTENT, "Unscheduled shutdown of Epilarm!\n (Restart via Epilarm app!)",
			"EPILARM_NOTIFICATION_CONTENT", NOTIFICATION_VARIABLE_TYPE_NONE);
	if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_ERROR, LOG_TAG, "could not create content of shutdown notification!"); return; }

	noti_err = notification_set_vibration(warn_notification, NOTIFICATION_VIBRATION_TYPE_DEFAULT, NULL);
	if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_ERROR, LOG_TAG, "could not set vibration of shutdown notification!"); return; }

	noti_err = notification_post(warn_notification);
	if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_ERROR, LOG_TAG, "could not post shutdown notification!"); return; }
}

void start_UI()
{
	app_control_h app_control = NULL;
	if (app_control_create(&app_control) == APP_CONTROL_ERROR_NONE)
	{
		//Setting an app ID.
		if (app_control_set_app_id(app_control, MYSERVICELAUNCHER_APP_ID) == APP_CONTROL_ERROR_NONE)
		{
			if(app_control_send_launch_request(app_control, NULL, NULL) == APP_CONTROL_ERROR_NONE)
			{
				dlog_print(DLOG_INFO, LOG_TAG, "App launch request sent!");
			}
		}
		if (app_control_destroy(app_control) == APP_CONTROL_ERROR_NONE)
		{
			dlog_print(DLOG_INFO, LOG_TAG, "App control destroyed.");
		}
	}
}

//sensor event callback implementation
void sensor_event_callback(sensor_h sensor, sensor_event_s *event, void *user_data)
{
	// Extracting application data
	appdata_s* ad = (appdata_s*)user_data;

	sensor_type_e type = SENSOR_ALL;

	if((sensor_get_type(sensor, &type) == SENSOR_ERROR_NONE) && type == SENSOR_LINEAR_ACCELERATION)
	{
		ringbuf_push(ad->rb_x, event->values[0]);
		ringbuf_push(ad->rb_y, event->values[1]);
		ringbuf_push(ad->rb_z, event->values[2]);

		//dlog_print(DLOG_INFO, LOG_TAG, "sensors read!");

		//each second perform fft analysis -> a second has passed if sampleRate number of new entries were made in rb_X, i.e., if rb_x->idx % sampleRate == 0
		if((ad->rb_x)->idx % sampleRate == 0)
		{
			//TODO remove after extensive testing!!!
			//share_data_mqtt(ad);

			//dlog_print(DLOG_INFO, LOG_TAG, "read out ringbufs...");
			ringbuf_get_buf(ad->rb_x, ad->fft_x_spec);
			ringbuf_get_buf(ad->rb_y, ad->fft_y_spec);
			ringbuf_get_buf(ad->rb_z, ad->fft_z_spec);
			//dlog_print(DLOG_INFO, LOG_TAG, "starting FFT...");
			fft_forward(ad->fft_x, ad->fft_x_spec);
			fft_forward(ad->fft_y, ad->fft_y_spec);
			fft_forward(ad->fft_z, ad->fft_z_spec);
			//dlog_print(DLOG_INFO, LOG_TAG, "FFT done.");

			//rearrange transform output s.t. sin and cos parts are not seperated, also normalize them, i.e., take their absolute values
			for (int i = 0; i < bufferSize/2; ++i) {
				ad->fft_x_spec[i] = sqrt(ad->fft_x_spec[2*i]*ad->fft_x_spec[2*i] + ad->fft_x_spec[2*i+1]*ad->fft_x_spec[2*i+1]);
				ad->fft_y_spec[i] = sqrt(ad->fft_y_spec[2*i]*ad->fft_y_spec[2*i] + ad->fft_y_spec[2*i+1]*ad->fft_y_spec[2*i+1]);
				ad->fft_z_spec[i] = sqrt(ad->fft_z_spec[2*i]*ad->fft_z_spec[2*i] + ad->fft_z_spec[2*i+1]*ad->fft_z_spec[2*i+1]);

			} //note fft_X_spec[i] now contains the magnitudes freq 0.1Hz*i (for i=0 ... sampleSize/2)

			//collect the freqs in bins of 0.5Hz each (in ad->fft_X_spec_simplified); they each have a total of sampleSize bins
			for (int i = 0; i < sampleRate; ++i) {
				//dlog_print(DLOG_INFO, LOG_TAG, "i: %i", i);
				ad->fft_x_spec_simplified[i] = 0;
				ad->fft_y_spec_simplified[i] = 0;
				ad->fft_z_spec_simplified[i] = 0;
				for (int j = i*dataAnalysisInterval/2 + (i==0 ? 1 : 0); j < (i+1)*dataAnalysisInterval/2; ++j) { //skip case j=0 as it contains movement corr to 0Hz movement
					//dlog_print(DLOG_INFO, LOG_TAG, " j: %i", j);
					ad->fft_x_spec_simplified[i] += ad->fft_x_spec[j];
					ad->fft_y_spec_simplified[i] += ad->fft_y_spec[j];
					ad->fft_z_spec_simplified[i] += ad->fft_z_spec[j];
				}
			}
			//dlog_print(DLOG_INFO, LOG_TAG, "spec simplified!");

			//first step of analysis: compute the average value among the relevant freqs, and
			//second step of analysis: compute the average value of all other freqs
			ad->avg_roi_x = 0;
			ad->avg_roi_y = 0;
			ad->avg_roi_z = 0;
			ad->avg_nroi_x = 0;
			ad->avg_nroi_y = 0;
			ad->avg_nroi_z = 0;

			double max_x = 0;
			double max_y = 0;
			double max_z = 0;
			for (int i = 2*ad->minFreq; i <= 2*ad->maxFreq; ++i) {
				ad->avg_roi_x += ad->fft_x_spec_simplified[i];
				ad->avg_roi_y += ad->fft_y_spec_simplified[i];
				ad->avg_roi_z += ad->fft_z_spec_simplified[i];
				if(ad->fft_x_spec_simplified[i] > max_x) max_x = ad->fft_x_spec_simplified[i];
				if(ad->fft_y_spec_simplified[i] > max_y) max_y = ad->fft_y_spec_simplified[i];
				if(ad->fft_z_spec_simplified[i] > max_z) max_z = ad->fft_z_spec_simplified[i];
			}
			for (int i = 0; i < sampleRate; ++i) {
				ad->avg_nroi_x += ad->fft_x_spec_simplified[i];
				ad->avg_nroi_y += ad->fft_y_spec_simplified[i];
				ad->avg_nroi_z += ad->fft_z_spec_simplified[i];
			}

			ad->avg_nroi_x = (ad->avg_nroi_x - ad->avg_roi_x) / (sampleRate-(2*ad->maxFreq-2*ad->minFreq+1));
			ad->avg_roi_x = ad->avg_roi_x / (2*ad->maxFreq-2*ad->minFreq+1);
			ad->avg_nroi_y = (ad->avg_nroi_y - ad->avg_roi_y) / (sampleRate-(2*ad->maxFreq-2*ad->minFreq+1));
			ad->avg_roi_y = ad->avg_roi_y / (2*ad->maxFreq-2*ad->minFreq+1);
			ad->avg_nroi_z = (ad->avg_nroi_z - ad->avg_roi_z) / (sampleRate-(2*ad->maxFreq-2*ad->minFreq+1));
			ad->avg_roi_z = ad->avg_roi_z / (2*ad->maxFreq-2*ad->minFreq+1);

			dlog_print(DLOG_INFO, LOG_TAG, "minfreq: %f, maxfeq: %f", ad->minFreq, ad->maxFreq);


			dlog_print(DLOG_INFO, LOG_TAG, "avg_nroi_x: %f, avgRoi_x: %f, (max_x_roi: %f)", ad->avg_nroi_x, ad->avg_roi_x, max_x);
			dlog_print(DLOG_INFO, LOG_TAG, "avg_nroi_y: %f, avgRoi_y: %f, (max_y_roi: %f)", ad->avg_nroi_y, ad->avg_roi_y, max_y);
			dlog_print(DLOG_INFO, LOG_TAG, "avg_nroi_z: %f, avgRoi_z: %f, (max_z_roi: %f)", ad->avg_nroi_z, ad->avg_roi_z, max_z);

			//print comp freqs 0-1Hz 1-2Hz 2-3Hz ... 9-10Hz
			dlog_print(DLOG_INFO, LOG_TAG, "x: %f  %f  %f  %f  %f  %f  %f  %f  %f  %f", ad->fft_x_spec_simplified[0]+ad->fft_x_spec_simplified[1], ad->fft_x_spec_simplified[2]+ad->fft_x_spec_simplified[3],
					ad->fft_x_spec_simplified[4]+ad->fft_x_spec_simplified[5], ad->fft_x_spec_simplified[6]+ad->fft_x_spec_simplified[7], ad->fft_x_spec_simplified[8]+ad->fft_x_spec_simplified[9],
					ad->fft_x_spec_simplified[10]+ad->fft_x_spec_simplified[11], ad->fft_x_spec_simplified[12]+ad->fft_x_spec_simplified[13], ad->fft_x_spec_simplified[14]+ad->fft_x_spec_simplified[15],
					ad->fft_x_spec_simplified[16]+ad->fft_x_spec_simplified[17], ad->fft_x_spec_simplified[18]+ad->fft_x_spec_simplified[19]);
			dlog_print(DLOG_INFO, LOG_TAG, "y: %f  %f  %f  %f  %f  %f  %f  %f  %f  %f", ad->fft_y_spec_simplified[0]+ad->fft_y_spec_simplified[1], ad->fft_y_spec_simplified[2]+ad->fft_y_spec_simplified[3],
					ad->fft_y_spec_simplified[4]+ad->fft_y_spec_simplified[5], ad->fft_y_spec_simplified[7]+ad->fft_y_spec_simplified[6], ad->fft_y_spec_simplified[9]+ad->fft_y_spec_simplified[8],
					ad->fft_y_spec_simplified[11]+ad->fft_y_spec_simplified[10], ad->fft_y_spec_simplified[13]+ad->fft_y_spec_simplified[12], ad->fft_y_spec_simplified[15]+ad->fft_y_spec_simplified[14],
					ad->fft_y_spec_simplified[17]+ad->fft_y_spec_simplified[16], ad->fft_y_spec_simplified[19]+ad->fft_y_spec_simplified[18]);
			dlog_print(DLOG_INFO, LOG_TAG, "z: %f  %f  %f  %f  %f  %f  %f  %f  %f  %f", ad->fft_z_spec_simplified[0]+ad->fft_z_spec_simplified[1], ad->fft_z_spec_simplified[2]+ad->fft_z_spec_simplified[3],
					ad->fft_z_spec_simplified[4]+ad->fft_z_spec_simplified[5], ad->fft_z_spec_simplified[7]+ad->fft_z_spec_simplified[6], ad->fft_z_spec_simplified[9]+ad->fft_z_spec_simplified[8],
					ad->fft_z_spec_simplified[11]+ad->fft_z_spec_simplified[10], ad->fft_z_spec_simplified[13]+ad->fft_z_spec_simplified[12], ad->fft_z_spec_simplified[15]+ad->fft_z_spec_simplified[14],
					ad->fft_z_spec_simplified[17]+ad->fft_z_spec_simplified[16], ad->fft_z_spec_simplified[19]+ad->fft_z_spec_simplified[18]);

			ad->multRatio = ((ad->avg_roi_x/ad->avg_nroi_x) + (ad->avg_roi_y/ad->avg_nroi_y) + (ad->avg_roi_z/ad->avg_nroi_z)) / 3.0;

			//combine three values for threshold comparison
			ad->avgRoi = sqrt(ad->avg_roi_x * ad->avg_roi_x + ad->avg_roi_y * ad->avg_roi_y + ad->avg_roi_z * ad->avg_roi_z);

			dlog_print(DLOG_INFO, LOG_TAG, "# multRatio: %f, avgRoi: %f", ad->multRatio, ad->avgRoi);
			if (ad->avgRoi >= ad->avgRoiThresh)
				dlog_print(DLOG_INFO, LOG_TAG, "---> avgRoiThresh reached");
			else
				dlog_print(DLOG_INFO, LOG_TAG, "---> avgRoiThresh NOT reached");

			if (ad->multRatio >= ad->multThresh)
				dlog_print(DLOG_INFO, LOG_TAG, "---> multThresh reached");
			else
				dlog_print(DLOG_INFO, LOG_TAG, "---> multThresh NOT reached");

			//check both conditions for increasing alarmstate
			if(ad->multRatio >= ad->multThresh && ad->avgRoi >= ad->avgRoiThresh) {
				ad->alarmState = ad->alarmState+1;
				dlog_print(DLOG_INFO, LOG_TAG, "### alarmState increased by one to %i", ad->alarmState);

				//TODO add appropriate handling for WARNING state (i.e. vibrate motors, display on, sounds?!)
				//navigator.vibrate(100);
				//send new notification
				issue_warning_notification(ad->alarmState);

				if(ad->alarmState >= ad->warnTime) {
					dlog_print(DLOG_INFO, LOG_TAG, "#### ALARM RAISED ####");
					//TODO add appropriate handling for ALARM state (i.e. contact persons based on GPS location?!)
					issue_alarm_notification(ad->alarmState);
					//navigator.vibrate(100);
					start_UI();
				}
			} else {
				if(ad->alarmState > 1) {
					ad->alarmState = ad->alarmState - 1;
					dlog_print(DLOG_INFO, LOG_TAG, "### alarmState decreased by one to %i", ad->alarmState);
				} else if (ad->alarmState == 1) {
					ad->alarmState = ad->alarmState - 1;
					dlog_print(DLOG_INFO, LOG_TAG, "### alarmState decreased by one to %i", ad->alarmState);
				}
			}

			if(ad->logging) {
				//write log to local storage after every analysis
				save_log(ad);
			}
		}
	}
}

bool service_app_create(void *data)
{
	// Extracting application data
	appdata_s* ad = (appdata_s*)data;

	bool sensor_supported = false;
	if (sensor_is_supported(SENSOR_LINEAR_ACCELERATION, &sensor_supported) != SENSOR_ERROR_NONE || sensor_supported == false)
	{
		dlog_print(DLOG_ERROR, LOG_TAG, "Accelerometer not supported! Service is useless, exiting...");
		service_app_exit();
		return false;
	}

	//create fft_transformers
	ad->fft_x = create_fft_transformer(bufferSize, FFT_SCALED_OUTPUT);
	ad->fft_y = create_fft_transformer(bufferSize, FFT_SCALED_OUTPUT);
	ad->fft_z = create_fft_transformer(bufferSize, FFT_SCALED_OUTPUT);
	dlog_print(DLOG_INFO, LOG_TAG, "fft transformers created.");


	//create ringbuffs
    ad->rb_x = ringbuf_new(bufferSize);
    ad->rb_y = ringbuf_new(bufferSize);
    ad->rb_z = ringbuf_new(bufferSize);
	dlog_print(DLOG_INFO, LOG_TAG, "ringbufs created.");

	//create tmp arrays for results from fft and their simplified version
    ad->fft_x_spec = malloc(sizeof(double)*(ad->fft_x)->n);
    ad->fft_y_spec = malloc(sizeof(double)*(ad->fft_y)->n);
    ad->fft_z_spec = malloc(sizeof(double)*(ad->fft_z)->n);

    ad->fft_x_spec_simplified = malloc(sizeof(double)*(sampleRate));
    ad->fft_y_spec_simplified = malloc(sizeof(double)*(sampleRate));
    ad->fft_z_spec_simplified = malloc(sizeof(double)*(sampleRate));

    ad->alarmState = 0;

    ad->running = false;

    ad->logging = false;

	return true;
}

void sensor_start(void *data)
{
	// Extracting application data
	appdata_s* ad = (appdata_s*)data;

	// Preparing and starting the sensor listener for the accelerometer.
	if (sensor_get_default_sensor(SENSOR_LINEAR_ACCELERATION, &(ad->sensor)) == SENSOR_ERROR_NONE)
	{
		if (sensor_create_listener(ad->sensor, &(ad->listener)) == SENSOR_ERROR_NONE
			&& sensor_listener_set_event_cb(ad->listener, dataQueryInterval, sensor_event_callback, ad) == SENSOR_ERROR_NONE
			&& sensor_listener_set_option(ad->listener, SENSOR_OPTION_ALWAYS_ON) == SENSOR_ERROR_NONE)
		{
			if (sensor_listener_start(ad->listener) == SENSOR_ERROR_NONE)
			{
				dlog_print(DLOG_INFO, LOG_TAG, "Sensor listener started.");
				ad->running = true;
			}
		}
	}
}

//input: appdata, bool that tells if a warning notification should be sent.
void sensor_stop(void *data, int sendNot)
{
	// Extracting application data
	appdata_s* ad = (appdata_s*)data;

	if(!ad->running) {
		dlog_print(DLOG_INFO, LOG_TAG, "Sensor listener already destroyed.");
		return;
	}
	//Stopping & destroying sensor listener
	if ((sensor_listener_stop(ad->listener) == SENSOR_ERROR_NONE)
		&& (sensor_destroy_listener(ad->listener) == SENSOR_ERROR_NONE))
	{
		dlog_print(DLOG_INFO, LOG_TAG, "Sensor listener destroyed.");
		ad->running = false;
		if (sendNot != 0) { //send notification that sensorlistener is destroyed
			dlog_print(DLOG_INFO, LOG_TAG, "Unscheduled shutdown of Epilarm-service! (Restart via UI!)");
			issue_unplanned_shutdown_notification();
			//TODO start UI and tell it that service app crashed!
			start_UI();
		}
	}
	else
	{
		dlog_print(DLOG_INFO, LOG_TAG, "Error occurred when destroying sensor listener: listener was never created!");
	}
}

void service_app_terminate(void *data)
{
	// Extracting application data
	appdata_s* ad = (appdata_s*)data;

	sensor_stop(data, 1); //if sensor is stopped - with a warning; if already stopped before, no warning

	//destroy fft_transformers
	free_fft_transformer(ad->fft_x);
	free_fft_transformer(ad->fft_y);
	free_fft_transformer(ad->fft_z);

	//destroy ringbufs
	ringbuf_free(&ad->rb_x);
	ringbuf_free(&ad->rb_y);
	ringbuf_free(&ad->rb_z);

	//destroy tmp arrays for results from fft and their simplified version
	free(ad->fft_x_spec);
	free(ad->fft_y_spec);
	free(ad->fft_z_spec);

	free(ad->fft_x_spec_simplified);
	free(ad->fft_y_spec_simplified);
	free(ad->fft_z_spec_simplified);
}

void service_app_control(app_control_h app_control, void *data)
{
	// Extracting application data
	appdata_s* ad = (appdata_s*)data;

    char *caller_id = NULL, *action_value = NULL;
    if ((app_control_get_caller(app_control, &caller_id) == APP_CONTROL_ERROR_NONE)
        && (app_control_get_extra_data(app_control, "service_action", &action_value) == APP_CONTROL_ERROR_NONE))
    {
    	dlog_print(DLOG_INFO, LOG_TAG, "caller_id = %s", caller_id);
    	dlog_print(DLOG_INFO, LOG_TAG, "action_value = %s", action_value);
        if((caller_id != NULL) && (action_value != NULL)
             && (!strncmp(caller_id, MYSERVICELAUNCHER_APP_ID, STRNCMP_LIMIT))
             && (!strncmp(action_value, "start", STRNCMP_LIMIT)))
        {
        	//get params from appcontrol: [minFreq, maxFreq, avgRoiThresh, multThresh, warnTime]
            dlog_print(DLOG_INFO, LOG_TAG, "Epilarm start! reading params...");
            char **params; int length;
        	if (app_control_get_extra_data_array(app_control, "params", &params, &length) == APP_CONTROL_ERROR_NONE)
        	{
        		if (length != 11) { dlog_print(DLOG_INFO, LOG_TAG, "received too few params!"); }
        		dlog_print(DLOG_INFO, LOG_TAG, "received %i params: minFreq=%s, maxFreq=%s, avgRoiThresh=%s, multThresh=%s, warnTime=%s, logging=%s, mqttBrokerAddress=%s, mqttPort=%s, mqttTopic=%s, mqttUsername=%s, mqttPassword=%s",
        				length, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8], params[9], params[10]);
        	    char *eptr;
        		//set new params
        		ad->minFreq = strtod(params[0],&eptr);
        		ad->maxFreq = strtod(params[1],&eptr);
        		ad->avgRoiThresh = strtod(params[2],&eptr);
        		ad->multThresh = strtod(params[3],&eptr);
        		ad->warnTime = atoi(params[4]);
        		ad->logging = atoi(params[5]);

        		dlog_print(DLOG_INFO, LOG_TAG, "Starting epilarm sensor service!");
        		sensor_start(ad);

        		if(ad->logging)
        		{
        			/* in case logging is enabled, setup a mqtt clients info and start sharing of data*/

        			ad->mqtt_state.hostname = params[6];
        			ad->mqtt_state.port =  params[7];
        			ad->mqtt_state.topic = params[8];
        			ad->mqtt_state.username = params[9];
        			ad->mqtt_state.password = params[10];
        			ad->mqtt_state.client_id = "sam_gal_act_2";
        			uint8_t sendbuf[128 * 2048];
        			uint8_t recvbuf[1024];
        			ad->mqtt_state.sendbuf = sendbuf;
        			ad->mqtt_state.sendbufsz = sizeof(sendbuf);
        			ad->mqtt_state.recvbuf = recvbuf;
        			ad->mqtt_state.recvbufsz = sizeof(recvbuf);

        			dlog_print(DLOG_INFO, LOG_TAG, "Starting mqtt daemon!");
        			dlog_print(DLOG_INFO, LOG_TAG_MQTT, "Starting mqtt daemon!");

        			start_mqtt_daemon(ad);
        		}

        	} else {
        		dlog_print(DLOG_INFO, LOG_TAG, "receiving params failed! sensor not started!");
        	}

        	free(params);

        } else if((caller_id != NULL) && (action_value != NULL)
             && (!strncmp(caller_id, MYSERVICELAUNCHER_APP_ID, STRNCMP_LIMIT))
             && (!strncmp(action_value, "stop", STRNCMP_LIMIT)))
        {
            dlog_print(DLOG_INFO, LOG_TAG, "Stopping epilarm sensor service!");
            sensor_stop(data, 0); //stop sensor listener without notification (as it was shut down on purpose)

            if(ad->logging) {
            	stop_mqtt_daemon(ad);
            }

            free(caller_id);
            free(action_value);
            service_app_exit(); //this also tries to stop the sensor listener, but will issue a warning in the log...
            return;
        } else if((caller_id != NULL) && (action_value != NULL)
                && (!strncmp(caller_id, MYSERVICELAUNCHER_APP_ID, STRNCMP_LIMIT))
                && (!strncmp(action_value, "running?", STRNCMP_LIMIT)))
        {
            dlog_print(DLOG_INFO, LOG_TAG, "are we running? (asked by UI)!");

        	char *app_id;
        	app_control_h reply;
    		app_control_create(&reply);
    		app_control_get_app_id(app_control, &app_id);

    		app_control_add_extra_data(reply, APP_CONTROL_DATA_SELECTED, ad->running ? "1" : "0");

    		app_control_reply_to_launch_request(reply, app_control, APP_CONTROL_RESULT_SUCCEEDED);
            dlog_print(DLOG_INFO, LOG_TAG, "reply sent");

    		app_control_destroy(reply);

            free(caller_id);
            free(action_value);
            return;
        } else {
            dlog_print(DLOG_INFO, LOG_TAG, "Unsupported action! Doing nothing...");
            free(caller_id);
            free(action_value);
            caller_id = NULL;
            action_value = NULL;
        }
    }
}

static void
service_app_lang_changed(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_LANGUAGE_CHANGED*/
	return;
}

static void
service_app_region_changed(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_REGION_FORMAT_CHANGED*/
}

static void
service_app_low_battery(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_LOW_BATTERY*/
}

static void
service_app_low_memory(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_LOW_MEMORY*/
}

int main(int argc, char* argv[])
{
	// we declare application data as a structure defined earlier
	appdata_s ad = {0,};

	service_app_lifecycle_callback_s event_callback = {0,};
	app_event_handler_h handlers[5] = {NULL, };

	event_callback.create = service_app_create;
	event_callback.terminate = service_app_terminate;
	event_callback.app_control = service_app_control;

	service_app_add_event_handler(&handlers[APP_EVENT_LOW_BATTERY], APP_EVENT_LOW_BATTERY, service_app_low_battery, &ad);
	service_app_add_event_handler(&handlers[APP_EVENT_LOW_MEMORY], APP_EVENT_LOW_MEMORY, service_app_low_memory, &ad);
	service_app_add_event_handler(&handlers[APP_EVENT_LANGUAGE_CHANGED], APP_EVENT_LANGUAGE_CHANGED, service_app_lang_changed, &ad);
	service_app_add_event_handler(&handlers[APP_EVENT_REGION_FORMAT_CHANGED], APP_EVENT_REGION_FORMAT_CHANGED, service_app_region_changed, &ad);

	// we keep a template code above and then modify the line below
	return service_app_main(argc, argv, &event_callback, &ad);
}
