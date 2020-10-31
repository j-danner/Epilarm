#include <tizen.h> // standard header from the template
#include <service_app.h> // standard header from the template
#include "epilarm_sensor_service.h"

// headers that will be needed for our service:
#include <sensor.h>
#include <stdlib.h>
#include <stdio.h>
#include <device/power.h>
#include <math.h>
#include <notification.h>


//own modules
#include <fft.h>
#include <rb.h>
#include <mqtt.h>
#include <unistd.h>
#include <posix_sockets.h>



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


// application data (context) that will be passed to functions when needed
typedef struct appdata
{
	sensor_h sensor; // sensor handle
	sensor_listener_h listener; // sensor listener handle

	//params of analysis (set by UI on startup)
	double minFreq;
	double maxFreq;
	double avgRoiThresh;
	double multThresh;
	int warnTime;
	//logging of data AND sending over mqtt to broker
	bool logging;
	char* mqttBroker;
	char* mqttPort;
	char* mqttTopic;
	char* mqttUsername;
	char* mqttPassword;

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


//locally save analyzed data
void saveData() {
}


void publish_callback(void** unused, struct mqtt_response_publish *published)
{
    /* not used in this example */
}

void* client_refresher(void* client)
{
    while(1)
    {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U);
    }
    return NULL;
}

//send data over mqqt to broker
void send_data(void *data) {
	// Extracting application data
	appdata_s* ad = (appdata_s*)data;

	int sockfd = open_nb_socket(ad->mqttBroker, ad->mqttPort);

	if (sockfd == -1) {
	   dlog_print(DLOG_INFO, LOG_TAG, "Failed to open socket!");
	   return;
	} else {
       dlog_print(DLOG_INFO, LOG_TAG, "Opened socket! (%d)", sockfd);
	}

	/* setup a client */
	struct mqtt_client client;
	uint8_t sendbuf[2048];  /* sendbuf should be large enough to hold multiple whole mqtt messages */
    uint8_t recvbuf[1024];  /* recvbuf should be large enough any whole mqtt message expected to be received */
	mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);
    dlog_print(DLOG_INFO, LOG_TAG, "initialized mqtt client!");
	/* Create an anonymous session */
	const char* client_id = "galaxy_active_2_epilarm";
	/* Ensure we have a clean session */
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
	/* Send connection request to the broker. */
	mqtt_connect(&client, client_id, NULL, NULL, 0, ad->mqttUsername, ad->mqttPassword, connect_flags, 400);
    dlog_print(DLOG_INFO, LOG_TAG, "mqtt connected!");

	/* check that we don't have any errors */
	if (client.error != MQTT_OK) {
		dlog_print(DLOG_INFO, LOG_TAG, "error: %s", mqtt_error_str(client.error));
	    close(sockfd);
		return;
	}

	/* start a thread to refresh the client (handle egress and ingree client traffic) */
	pthread_t client_daemon;
	if(pthread_create(&client_daemon, NULL, client_refresher, &client)) {
		dlog_print(DLOG_INFO, LOG_TAG, "Failed to start client daemon.");
	    close(sockfd);
		return;
	}

	/* start publishing the time */
    time_t timer;
    time(&timer);
    struct tm* tm_info = localtime(&timer);
    char timebuf[26];
    strftime(timebuf, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    /* print a message */
    char application_message[256];
    snprintf(application_message, sizeof(application_message), "The time is %s", timebuf);

	dlog_print(DLOG_INFO, LOG_TAG, "service is publishing message='%s' under topic='%s'.", application_message, ad->mqttTopic);

	/* publish the test message */
	mqtt_publish(&client, ad->mqttTopic, application_message, strlen(application_message) + 1, MQTT_PUBLISH_QOS_0);
	dlog_print(DLOG_INFO, LOG_TAG, "message published!");

	/* check for errors */
	if (client.error != MQTT_OK) {
		dlog_print(DLOG_INFO, LOG_TAG, "error: %s", mqtt_error_str(client.error));
	    close(sockfd);
	    pthread_cancel(client_daemon);
	}

	/* disconnect */
	dlog_print(DLOG_INFO, LOG_TAG, "service disconnecting from %s", ad->mqttBroker);
	mqtt_disconnect(&client);
	sleep(1);

	/* exit */
    close(sockfd);
    pthread_cancel(client_daemon);
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

		//each second perform fft analysis -> a second has passed if sampleRate number of new entries were made in rb_X, i.e., if rb_x->idx = sampleRate
		if((ad->rb_x)->idx % sampleRate == 0)
		{
			//TODO remove after extensive testing!!!
			send_data(ad);

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
			double avg_roi_x = 0;
			double avg_nroi_x = 0;
			double avg_roi_y = 0;
			double avg_nroi_y = 0;
			double avg_roi_z = 0;
			double avg_nroi_z = 0;

			double max_x = 0;
			double max_y = 0;
			double max_z = 0;
			for (int i = 2*ad->minFreq; i <= 2*ad->maxFreq; ++i) {
				avg_roi_x += ad->fft_x_spec_simplified[i];
				avg_roi_y += ad->fft_y_spec_simplified[i];
				avg_roi_z += ad->fft_z_spec_simplified[i];
				if(ad->fft_x_spec_simplified[i] > max_x) max_x = ad->fft_x_spec_simplified[i];
				if(ad->fft_y_spec_simplified[i] > max_y) max_y = ad->fft_y_spec_simplified[i];
				if(ad->fft_z_spec_simplified[i] > max_z) max_z = ad->fft_z_spec_simplified[i];
			}
			for (int i = 0; i < sampleRate; ++i) {
				avg_nroi_x += ad->fft_x_spec_simplified[i];
				avg_nroi_y += ad->fft_y_spec_simplified[i];
				avg_nroi_z += ad->fft_z_spec_simplified[i];
			}

			avg_nroi_x = (avg_nroi_x-avg_roi_x) / (sampleRate-(2*ad->maxFreq-2*ad->minFreq+1));
			avg_roi_x = avg_roi_x / (2*ad->maxFreq-2*ad->minFreq+1);
			avg_nroi_y = (avg_nroi_y-avg_roi_y) / (sampleRate-(2*ad->maxFreq-2*ad->minFreq+1));
			avg_roi_y = avg_roi_y / (2*ad->maxFreq-2*ad->minFreq+1);
			avg_nroi_z = (avg_nroi_z-avg_roi_z) / (sampleRate-(2*ad->maxFreq-2*ad->minFreq+1));
			avg_roi_z = avg_roi_z / (2*ad->maxFreq-2*ad->minFreq+1);

			dlog_print(DLOG_INFO, LOG_TAG, "minfreq: %f, maxfeq: %f", ad->minFreq, ad->maxFreq);


			dlog_print(DLOG_INFO, LOG_TAG, "avg_nroi_x: %f, avg_roi_x: %f, (max_x_roi: %f)", avg_nroi_x, avg_roi_x, max_x);
			dlog_print(DLOG_INFO, LOG_TAG, "avg_nroi_y: %f, avg_roi_y: %f, (max_y_roi: %f)", avg_nroi_y, avg_roi_y, max_y);
			dlog_print(DLOG_INFO, LOG_TAG, "avg_nroi_z: %f, avg_roi_z: %f, (max_z_roi: %f)", avg_nroi_z, avg_roi_z, max_z);

			//print comp freqs 0-1Hz 1-2Hz 2-3Hz ... 9-10Hz
			dlog_print(DLOG_INFO, LOG_TAG, "x: %f  %f  %f  %f  %f  %f  %f  %f  %f  %f", ad->fft_x_spec_simplified[0]+ad->fft_x_spec_simplified[1], ad->fft_x_spec_simplified[2]+ad->fft_x_spec_simplified[3],
					ad->fft_x_spec_simplified[4]+ad->fft_x_spec_simplified[5], ad->fft_x_spec_simplified[7]+ad->fft_x_spec_simplified[8], ad->fft_x_spec_simplified[9]+ad->fft_x_spec_simplified[10],
					ad->fft_x_spec_simplified[11]+ad->fft_x_spec_simplified[12], ad->fft_x_spec_simplified[13]+ad->fft_x_spec_simplified[14], ad->fft_x_spec_simplified[15]+ad->fft_x_spec_simplified[16],
					ad->fft_x_spec_simplified[17]+ad->fft_x_spec_simplified[18], ad->fft_x_spec_simplified[19]+ad->fft_x_spec_simplified[20]);
			dlog_print(DLOG_INFO, LOG_TAG, "y: %f  %f  %f  %f  %f  %f  %f  %f  %f  %f", ad->fft_y_spec_simplified[0]+ad->fft_y_spec_simplified[1], ad->fft_y_spec_simplified[2]+ad->fft_y_spec_simplified[3],
					ad->fft_y_spec_simplified[4]+ad->fft_y_spec_simplified[5], ad->fft_y_spec_simplified[7]+ad->fft_y_spec_simplified[8], ad->fft_y_spec_simplified[9]+ad->fft_y_spec_simplified[10],
					ad->fft_y_spec_simplified[11]+ad->fft_y_spec_simplified[12], ad->fft_y_spec_simplified[13]+ad->fft_y_spec_simplified[14], ad->fft_y_spec_simplified[15]+ad->fft_y_spec_simplified[16],
					ad->fft_y_spec_simplified[17]+ad->fft_y_spec_simplified[18], ad->fft_y_spec_simplified[19]+ad->fft_y_spec_simplified[20]);
			dlog_print(DLOG_INFO, LOG_TAG, "z: %f  %f  %f  %f  %f  %f  %f  %f  %f  %f", ad->fft_z_spec_simplified[0]+ad->fft_z_spec_simplified[1], ad->fft_z_spec_simplified[2]+ad->fft_z_spec_simplified[3],
					ad->fft_z_spec_simplified[4]+ad->fft_z_spec_simplified[5], ad->fft_z_spec_simplified[7]+ad->fft_z_spec_simplified[8], ad->fft_z_spec_simplified[9]+ad->fft_z_spec_simplified[10],
					ad->fft_z_spec_simplified[11]+ad->fft_z_spec_simplified[12], ad->fft_z_spec_simplified[13]+ad->fft_z_spec_simplified[14], ad->fft_z_spec_simplified[15]+ad->fft_z_spec_simplified[16],
					ad->fft_z_spec_simplified[17]+ad->fft_z_spec_simplified[18], ad->fft_z_spec_simplified[19]+ad->fft_z_spec_simplified[20]);

			double multRatio = ((avg_roi_x/avg_nroi_x) + (avg_roi_y/avg_nroi_y) + (avg_roi_z/avg_nroi_z)) / 3;

			//combine three values for threshold comparison
			double avg_roi = sqrt(avg_roi_x*avg_roi_x + avg_roi_y*avg_roi_y + avg_roi_z*avg_roi_z);

			dlog_print(DLOG_INFO, LOG_TAG, "# multRatio: %f, avg_roi: %f", multRatio, avg_roi);
			if (avg_roi >= ad->avgRoiThresh)
				dlog_print(DLOG_INFO, LOG_TAG, "---> avgRoiThresh reached");
			else
				dlog_print(DLOG_INFO, LOG_TAG, "---> avgRoiThresh NOT reached");

			if (multRatio >= ad->multThresh)
				dlog_print(DLOG_INFO, LOG_TAG, "---> multThresh reached");
			else
				dlog_print(DLOG_INFO, LOG_TAG, "---> multThresh NOT reached");

			//check both conditions for increasing alarmstate
			if(multRatio >= ad->multThresh && avg_roi >= ad->avgRoiThresh) {
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
		}
		//logging of data:
		if(ad->logging) {
			//TODO write to local storage
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
        		ad->mqttBroker = params[6];
        		ad->mqttPort = params[7];
        		ad->mqttTopic = params[8];
        		ad->mqttUsername = params[9];
        		ad->mqttPassword = params[10];


        		dlog_print(DLOG_INFO, LOG_TAG, "Starting epilarm sensor service!");
        		sensor_start(ad);
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
