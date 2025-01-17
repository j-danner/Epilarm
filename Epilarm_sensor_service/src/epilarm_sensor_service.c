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
#include <string.h>
//tizen...
#include <sensor.h>
#include <device/power.h>
#include <device/battery.h>
#include <device/haptic.h>
#include <notification.h>

//ftp upload
#include <curl/curl.h>
#include <sys/stat.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>

//compression
#define _FILE_OFFSET_BITS 64
#include <zlib.h>
#include <microtar.h>

//FFT
#include <fft.h>
//RingBuffer
#include <rb.h>

//debugging...
#include <assert.h>



#define BUFLEN 16384
#define MAX_PATH 256
#define MAX_URL_LEN 256
#define MAX_LOG_LEN 4096



// some constant values used in the app
#define MYSERVICELAUNCHER_APP_ID "YSjzGlSXnd.Epilarm" // an ID of the UI application of our package
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
//                                        larger than some fixed multiple of the freqs of all other freqs.

//buffers for the three linear acceleration measurements
int bufferSize = sampleRate*dataAnalysisInterval; //size of stored data on which we perform the FFT


// application data (context) that will be passed to functions when needed
typedef struct appdata
{
  sensor_h sensor; // sensor handle
  sensor_listener_h listener; // sensor listener handle, must always be NULL if listener is not running!

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
  //logging of data AND sending over ftp to broker
  bool logging;

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

  //notification handles
  notification_h alarm_notification;
  notification_h shutdown_notification;
  notification_h warn_notification;
} appdata_s;


//get path where logs should be saved, log_path should have size at least MAX_PATH
void get_log_path(char* log_path) {
  char* data_path = app_get_shared_data_path();
  snprintf(log_path, MAX_PATH*sizeof(char), "%slogs", data_path);
  free(data_path);
}

//write timestamp in timebuf (it should have size at least 30!)
void get_timestamp(char* timebuf) {
  struct timespec tmnow;
  struct tm *tm;
  char nsec_buf[6];
  clock_gettime(CLOCK_REALTIME, &tmnow);
  tm = localtime(&tmnow.tv_sec);
  strftime(timebuf, 30, "%Y-%m-%dT%H:%M:%S.", tm);
  sprintf(nsec_buf, "%03d", (int) round(tmnow.tv_nsec/1000000));
  strncat(timebuf, nsec_buf, sizeof(timebuf)-sizeof(nsec_buf)-1);
}

//locally save analyzed data (called after each analysis and stored locally) -> CAUTION: must not be called more than once per second!
void save_log(void *data) {
  // Extracting application data
  appdata_s* ad = (appdata_s*)data;

  //get timestamp:
  char timebuf[30];
  get_timestamp(timebuf);

  //read battery status
  int battery_status = -1;
  device_battery_get_percent(&battery_status);

  //find appropriate file name and path
  char file_path[MAX_PATH];
  char log_path[MAX_PATH];
  get_log_path(log_path);
  snprintf(file_path, sizeof(file_path), "%s/log_%s.%s", log_path, timebuf, "json");

  dlog_print(DLOG_INFO, LOG_TAG, "save_log: data_path=%s", file_path);

  //print contents in json-format using json-glib
  JsonBuilder *builder = json_builder_new ();

  json_builder_begin_object (builder);
  //add timestamp
  json_builder_set_member_name (builder, "time");
  json_builder_add_string_value (builder, timebuf);

  //add battery percentage
  json_builder_set_member_name (builder, "bat");
  json_builder_add_int_value (builder, battery_status);

  //add alarmstate
  json_builder_set_member_name (builder, "a-st");
  json_builder_add_int_value (builder, ad->alarmState);

  /*
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
  */

  //include the bins of fft_x_spec_simplified corresponding to the first 25 bins (so in the 0-12.5Hz range)
  json_builder_set_member_name (builder, "x_spec");
  json_builder_begin_array(builder);
  for (int i = 0; i < 25; ++i) {
    json_builder_add_double_value(builder, ad->fft_x_spec_simplified[i]);
  }
  json_builder_end_array(builder);

  //include the bins of fft_y_spec_simplified corresponding to the first 40 bins (so in the 0-25Hz range)
  json_builder_set_member_name (builder, "y_spec");
  json_builder_begin_array(builder);
  for (int i = 0; i < 2*25; ++i) {
    json_builder_add_double_value(builder, ad->fft_y_spec_simplified[i]);
  }
  json_builder_end_array(builder);

  //include the bins of fft_z_spec_simplified corresponding to the first 40 bins (so in the 0-20Hz range)
  json_builder_set_member_name (builder, "z_spec");
  json_builder_begin_array(builder);
  for (int i = 0; i < 2*25; ++i) {
    json_builder_add_double_value(builder, ad->fft_z_spec_simplified[i]);
  }
  json_builder_end_array(builder);


  //include params of analysis (in one array)
  json_builder_set_member_name (builder, "params");
  json_builder_begin_array(builder);
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "minF");
  json_builder_add_double_value(builder, ad->minFreq);
  json_builder_set_member_name(builder, "maxF");
  json_builder_add_double_value(builder, ad->maxFreq);
  json_builder_set_member_name(builder, "aRT");
  json_builder_add_double_value(builder, ad->avgRoiThresh);
  json_builder_set_member_name(builder, "mT");
  json_builder_add_double_value(builder, ad->multThresh);
  json_builder_set_member_name(builder, "wT");
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

  //dlog_print(DLOG_INFO, LOG_TAG, "save_log: content with len %d is = '%s'", strlen(str), str);
  assert(strlen(str) < MAX_LOG_LEN);

  //write contents to file
  FILE *fp;
  fp = fopen(file_path, "w");
  fputs(str, fp);
  fclose(fp);
}


//deletes ALL log-files
int delete_logs() {
  if(device_power_request_lock(POWER_LOCK_CPU, 0) != DEVICE_ERROR_NONE)
  {
    dlog_print(DLOG_INFO, LOG_TAG, "could not lock CPU for log compression!");
    return -1;
  }

  dlog_print(DLOG_INFO, LOG_TAG, "delete_logs: removing log-files.");

  char file_path[MAX_PATH];
  char log_path[MAX_PATH];
  get_log_path(log_path);

  //iterate over files
  DIR *d;
  struct dirent *dir;

  //traverse log-files and delete them
  d = opendir(log_path);
  if(!d) dlog_print(DLOG_INFO, LOG_TAG, "could not open directory to delete log-files!"); //since we opened them just before, we should still be able to open them ;)
  while ((dir = readdir(d)) != NULL) {
    if (strncmp(dir->d_name,"log_",4) == 0 && strcmp(strrchr(dir->d_name, '.'), ".json") == 0)
    {
    //set correct file_path
    snprintf(file_path, sizeof(file_path), "%s%s", log_path, dir->d_name);
    //remove log file
    remove(file_path);
    //WARNING! here ALL files are deleted, it may happen that if tar is too large, some logs are skipped, this - however - only
    //         comes into play, when more than 2^21 logs have to be tarred. This is data worth ~3.5 weeks of continuous logging, so we may ignore this case!
    //         (I suspect that the space on the watch runs out before that many logs are created anyways^^)
    }
  }
  closedir(d);

  dlog_print(DLOG_INFO, LOG_TAG, "delete_logs: deleted all log-files.");

  device_power_release_lock(POWER_LOCK_CPU);

  return 0;
}


//compress all locally stored logs (all files ending with .json) (after successful compression, log-files are deleted)
int compress_logs() {
  dlog_print(DLOG_INFO, LOG_TAG, "compress_logs: start");

  if(device_power_request_lock(POWER_LOCK_CPU, 0) != DEVICE_ERROR_NONE)
  {
    dlog_print(DLOG_INFO, LOG_TAG, "could not lock CPU for log compression!");
    return -1;
  }

  char file_path[MAX_PATH];
  char log_path[MAX_PATH];
  get_log_path(log_path);
  char tar_path[MAX_PATH];
  char buff[MAX_LOG_LEN]; //must be large enough to fit one log-file (!!)

  //create tar file with appropriate name
  //get timestamp:
  char timebuf[30];
  get_timestamp(timebuf);
  snprintf(tar_path, sizeof(tar_path), "%s/logs_%s.tar", log_path, timebuf);
  dlog_print(DLOG_INFO, LOG_TAG, "tarpath=%s", tar_path);


  mtar_t tar;
  mtar_open(&tar, tar_path, "wb");

  //iterate over files
  struct stat file_info;
  FILE *fd;
  DIR *d;
  struct dirent *dir;

  int no_files_tar = 0;
  size_t tar_size = 0;

  d = opendir(log_path);
  dlog_print(DLOG_INFO, LOG_TAG, "reading data in %s", log_path);
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      //now dir->d_name stores name of file in dir
      if (strncmp(dir->d_name,"log_",4) == 0 && strcmp(strrchr(dir->d_name, '.'), ".json") == 0)
      {
        dlog_print(DLOG_INFO, LOG_TAG, "processing file %s", dir->d_name);
        //set correct file_path
        snprintf(file_path, sizeof(file_path), "%s/%s", log_path, dir->d_name);
        dlog_print(DLOG_INFO, LOG_TAG, "reading %s", file_path);

        fd = fopen(file_path, "rb"); /* open file to add to tar */
        if(!fd) {
          dlog_print(DLOG_WARN, LOG_TAG, "could not open file! (skipping file!)");
          continue; //skip this file!
        }

        /* to get the file size */
        if(fstat(fileno(fd), &file_info) != 0) {
          dlog_print(DLOG_WARN, LOG_TAG, "file is empty? (skipping file!)");
          fclose(fd);
          continue; //skip file
        }
        dlog_print(DLOG_INFO, LOG_TAG, "log-file %s has size %d.", dir->d_name, file_info.st_size);

        //read data
        fgets(buff, file_info.st_size+1, fd);
        fclose(fd);
        tar_size = tar_size + file_info.st_size+1;

        //attach file to tar
        mtar_write_file_header(&tar, dir->d_name, file_info.st_size);
        mtar_write_data(&tar, buff, file_info.st_size);
        no_files_tar = no_files_tar + 1;

        //remove local log file
        remove(file_path);
        dlog_print(DLOG_INFO, LOG_TAG, "%d: log-file %s added to tar. (total size: %d)", no_files_tar, dir->d_name, tar_size);

        //end appending of logs, if tar gets too large ,i.e., larger than 2^32 bytes.
        if (no_files_tar > 2097151) { // 2^32/2048 = 2097152
          break;
        }
      } else {
        //dir is either no regular file, its name is shorter than 5 chars or it does not end with .json
        dlog_print(DLOG_INFO, LOG_TAG, "skipping 'file' %s", dir->d_name);
      }
    }
    closedir(d);
  } else {
    dlog_print(DLOG_INFO, LOG_TAG, "directory does not exist or cannot be opened!");
    mtar_finalize(&tar);
    mtar_close(&tar);

    device_power_release_lock(POWER_LOCK_CPU);
    return -1;
  }

  /* Finalize -- this needs to be the last thing done before closing */
  mtar_finalize(&tar);
  /* Close archive */
  mtar_close(&tar);

  dlog_print(DLOG_INFO, LOG_TAG, "tarring %d log-files finished of total size %d", no_files_tar, tar_size);

  char tar_gz_path[MAX_PATH];
  snprintf(tar_gz_path, sizeof(tar_gz_path), "%s.gz", tar_path);

  dlog_print(DLOG_INFO, LOG_TAG, "compression started. (%s)", tar_gz_path);

  //code taken from function 'gz_compress' of minigzip ('https://github.com/madler/zlib/blob/master/test/minigzip.c')
  FILE   *in = fopen(tar_path, "rb");
  gzFile out = gzopen(tar_gz_path, "w");

  char buf[BUFLEN];
  int len;
  int err;
  for (;;) {
    len = (int)fread(buf, 1, sizeof(buf), in);
    if (ferror(in)) {
      dlog_print(DLOG_INFO, LOG_TAG, "error in fread when compressing!");
        return -1;
    }
    if (len == 0) break;

    if (gzwrite(out, buf, (unsigned)len) != len) dlog_print(DLOG_INFO, LOG_TAG, "gzerror: %s", gzerror(out, &err));
  }
  fclose(in);
  if (gzclose(out) != Z_OK) {
    dlog_print(DLOG_INFO, LOG_TAG, "failed gzclose");

    device_power_release_lock(POWER_LOCK_CPU);
    return -1;
  }

  dlog_print(DLOG_INFO, LOG_TAG, "remove tar-file.");
  //remove tar-file
  remove(tar_path);

  device_power_release_lock(POWER_LOCK_CPU);

  dlog_print(DLOG_INFO, LOG_TAG, "compress_logs: compression finished.");

  return delete_logs();
}



//function for sharing locally stored zip-data (logs first need to be zipped via compress_logs)
//returns -1 if it did not finish because of FTP server issues
//returns 0 otherwise
int share_data(const char* ftp_url) {
  dlog_print(DLOG_INFO, LOG_TAG, "share_data: start");

  if(device_power_request_lock(POWER_LOCK_CPU, 0) != DEVICE_ERROR_NONE)
  {
    dlog_print(DLOG_INFO, LOG_TAG, "could not lock CPU for data sharing!");
  }

  curl_global_init(CURL_GLOBAL_ALL);

  //init vars for ftp transfer...
  CURL *curl;
  CURLcode res;
  struct stat file_info;
  double speed_upload, total_time;
  FILE *fd;

  //get local data path
  char file_path[MAX_PATH];
  char log_path[MAX_PATH];
  get_log_path(log_path);
  char URL[MAX_URL_LEN]; //should be large enough to store ad->ftp_url + filename!

  //iterate over files:
  DIR *d;
  struct dirent *dir;
  d = opendir(log_path);
  dlog_print(DLOG_INFO, LOG_TAG, "reading data in %s", log_path);
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      //now dir->d_name stores name of file in dir
      if (strncmp(dir->d_name, "logs_",5) == 0 && strcmp(strrchr(dir->d_name, '.'), ".gz") == 0)
      {
        dlog_print(DLOG_INFO, LOG_TAG, "processing file %s", dir->d_name);
        //set correct file_path
        snprintf(file_path, sizeof(file_path), "%s/%s", log_path, dir->d_name);

        dlog_print(DLOG_INFO, LOG_TAG, "reading %s", file_path);
        fd = fopen(file_path, "rb"); /* open file to upload */
        if(!fd) {
          dlog_print(DLOG_INFO, LOG_TAG, "could not open file!");
          device_power_release_lock(POWER_LOCK_CPU);
          return -1; /* can't continue */
        }

        /* to get the file size */
        if(fstat(fileno(fd), &file_info) != 0) {
          dlog_print(DLOG_INFO, LOG_TAG, "file is empty?");
          device_power_release_lock(POWER_LOCK_CPU);
          fclose(fd);
          return -1; /* can't continue */
        }

        curl = curl_easy_init();
        if(curl)
        {
          //construct url
          dlog_print(DLOG_INFO, LOG_TAG, "ftp_url=%s", ftp_url);
          dlog_print(DLOG_INFO, LOG_TAG, "d_name=%s", dir->d_name);

          strcpy(URL, ftp_url);
          strcat(URL, dir->d_name);
          dlog_print(DLOG_INFO, LOG_TAG, "assembled URL=%s", URL);

          // try using ssl
          curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);

          /* upload to this place */
          curl_easy_setopt(curl, CURLOPT_URL, URL);
          // create directory if it does not exist
          curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR_RETRY);

          /* tell it to "upload" to the URL */
          curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

          /* set where to read from (on Windows you need to use READFUNCTION too) */
          curl_easy_setopt(curl, CURLOPT_READDATA, fd);

          /* and give the size of the upload (optional) */
          curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_info.st_size);

          /* enable verbose for easier tracing */
          curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

          res = curl_easy_perform(curl);
          /* Check for errors */
          if(res != CURLE_OK) {
            dlog_print(DLOG_INFO, LOG_TAG, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            device_power_release_lock(POWER_LOCK_CPU);
            curl_easy_cleanup(curl);
            fclose(fd);
            return -1;
          } else {
            dlog_print(DLOG_INFO, LOG_TAG, "file uploaded! (size=%d)", file_info.st_size);
            /* now extract transfer info */
            curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD, &speed_upload);
            curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);

            dlog_print(DLOG_INFO, LOG_TAG, "Speed: %.0f bytes/sec during %.1f seconds", speed_upload, total_time);

            remove(file_path);
            dlog_print(DLOG_INFO, LOG_TAG, "local file deleted.");
          }
          /* always cleanup */
          curl_easy_cleanup(curl);
        }
        fclose(fd);
      } else {
        //dir is either no regular file, its name is shorter than 5 chars or it does not end with .json
        //dlog_print(DLOG_INFO, LOG_TAG, "skipping 'file' %s", dir->d_name);
      }
    }
    closedir(d);
  } else {
    dlog_print(DLOG_INFO, LOG_TAG, "directory does not exist or cannot be opened!");
    device_power_release_lock(POWER_LOCK_CPU);
    return -1;
  }

  curl_global_cleanup();

  //release cpu-lock
  device_power_release_lock(POWER_LOCK_CPU);

  return 0;
}


//publish notification after updating its content with info on the current alarmstate
void publish_updated_notification(notification_h noti, int as) {
  int noti_err = notification_set_text(noti, NOTIFICATION_TEXT_TYPE_CONTENT, "Seizure-like movements detected! (alarmState = %d)",
	"EPILARM_NOTIFICATION_CONTENT", NOTIFICATION_VARIABLE_TYPE_INT, as, NOTIFICATION_VARIABLE_TYPE_NONE);
  if (noti_err != NOTIFICATION_ERROR_NONE) dlog_print(DLOG_ERROR, LOG_TAG, "could not create content of alarm notification!");
  noti_err = notification_post(noti);
  if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_ERROR, LOG_TAG, "could not post alarm notification!"); return; }
}

//publish notification
void publish_notification(notification_h noti) {
  int noti_err = notification_post(noti);
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

//vibrates for 'duration' ms with
static void device_vibrate(int duration, int feedback) {
  haptic_device_h haptic_handle;
  haptic_effect_h effect_handle;

  if(device_haptic_open(0, &haptic_handle) == DEVICE_ERROR_NONE) {
    dlog_print(DLOG_INFO, LOG_TAG, "Connection to vibrator established");

    if(device_haptic_vibrate(haptic_handle, duration, feedback, &effect_handle) == DEVICE_ERROR_NONE) {
      dlog_print(DLOG_INFO, LOG_TAG, "Device vibrates!");
    }
  } else {
    dlog_print(DLOG_INFO, LOG_TAG, "Connection to vibrator could not be established!");
  }
}

//given sensor data (with user_data) performs FFT and our seizure detection on this. Also handles raising of alarms.
void seizure_detection(void *data) {
  // Extracting application data
  appdata_s* ad = (appdata_s*)data;

  //dlog_print(DLOG_INFO, LOG_TAG, "read out ringbufs...");
  ringbuf_get_buf(ad->rb_x, ad->fft_x_spec);
  ringbuf_get_buf(ad->rb_y, ad->fft_y_spec);
  ringbuf_get_buf(ad->rb_z, ad->fft_z_spec);
  //dlog_print(DLOG_INFO, LOG_TAG, "starting FFT...");
  fft_forward(ad->fft_x, ad->fft_x_spec);
  fft_forward(ad->fft_y, ad->fft_y_spec);
  fft_forward(ad->fft_z, ad->fft_z_spec);
  //dlog_print(DLOG_INFO, LOG_TAG, "FFT done.");

  //rearrange transform output s.t. sin and cos parts are not separated, also normalize them, i.e., take their absolute values
  for (int i = 0; i < bufferSize/2; ++i) {
    ad->fft_x_spec[i] = sqrt(ad->fft_x_spec[2*i]*ad->fft_x_spec[2*i] + ad->fft_x_spec[2*i+1]*ad->fft_x_spec[2*i+1]);
    ad->fft_y_spec[i] = sqrt(ad->fft_y_spec[2*i]*ad->fft_y_spec[2*i] + ad->fft_y_spec[2*i+1]*ad->fft_y_spec[2*i+1]);
    ad->fft_z_spec[i] = sqrt(ad->fft_z_spec[2*i]*ad->fft_z_spec[2*i] + ad->fft_z_spec[2*i+1]*ad->fft_z_spec[2*i+1]);

  }
  //note fft_X_spec[i] now contains the magnitudes freq 0.1Hz*i (for i=0 ... sampleSize/2)

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

  int minfreq_ = 2*ad->minFreq;
  int maxfreq_ = 2*ad->maxFreq;
  for (int i = 0; i < 2*25; ++i) {
    if (minfreq_ <= i && i<=maxfreq_) {
      ad->avg_roi_x += ad->fft_x_spec_simplified[i];
      ad->avg_roi_y += ad->fft_y_spec_simplified[i];
      ad->avg_roi_z += ad->fft_z_spec_simplified[i];
    } else {
      ad->avg_nroi_x += ad->fft_x_spec_simplified[i];
      ad->avg_nroi_y += ad->fft_y_spec_simplified[i];
      ad->avg_nroi_z += ad->fft_z_spec_simplified[i];
    }
  }
  ad->avg_nroi_x = ad->avg_nroi_x / (2*25-(maxfreq_-minfreq_));
  ad->avg_roi_x = ad->avg_roi_x / (maxfreq_-minfreq_+1);
  ad->avg_nroi_y = ad->avg_nroi_y / (2*25-(maxfreq_-minfreq_));
  ad->avg_roi_y = ad->avg_roi_y / (maxfreq_-minfreq_+1);
  ad->avg_nroi_z = ad->avg_nroi_z / (2*25-(maxfreq_-minfreq_));
  ad->avg_roi_z = ad->avg_roi_z / (maxfreq_-minfreq_+1);

  /*
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
  */


  ad->multRatio = ((ad->avg_roi_x/ad->avg_nroi_x) + (ad->avg_roi_y/ad->avg_nroi_y) + (ad->avg_roi_z/ad->avg_nroi_z)) / 3.0;

  //combine three values for threshold comparison
  ad->avgRoi = sqrt(ad->avg_roi_x * ad->avg_roi_x + ad->avg_roi_y * ad->avg_roi_y + ad->avg_roi_z * ad->avg_roi_z);

  dlog_print(DLOG_INFO, LOG_TAG, "# multRatio: %f, avgRoi: %f", ad->multRatio, ad->avgRoi);
  if (ad->avgRoi >= ad->avgRoiThresh)
    dlog_print(DLOG_WARN, LOG_TAG, "---> avgRoiThresh reached");
  //else
  //  dlog_print(DLOG_INFO, LOG_TAG, "---> avgRoiThresh NOT reached");

  if (ad->multRatio >= ad->multThresh)
    dlog_print(DLOG_WARN, LOG_TAG, "---> multThresh reached");
  //else
  //  dlog_print(DLOG_INFO, LOG_TAG, "---> multThresh NOT reached");

  //check both conditions for increasing alarmstate
  if(ad->multRatio >= ad->multThresh && ad->avgRoi >= ad->avgRoiThresh) {
    ad->alarmState = ad->alarmState+1;
    dlog_print(DLOG_WARN, LOG_TAG, "### alarmState increased by one to %i", ad->alarmState);

    if(ad->alarmState >= ad->warnTime) {
      dlog_print(DLOG_WARN, LOG_TAG, "#### ALARM RAISED ####");
      //TODO add appropriate handling for ALARM state (i.e. contact persons based on GPS location?!)
      publish_updated_notification(ad->alarm_notification, ad->alarmState);
      //device_vibrate(800, 100);
      //TODO seizure detected! handle this appropriately in UI!
      start_UI();
    } else {
    	//alarm not raised BUT now in warning state!

        //TODO add appropriate handling for WARNING state (i.e. vibrate motors, display on, sounds?!)
        //device_vibrate(100, 100);
        //send notification
        publish_updated_notification(ad->warn_notification, ad->alarmState);
    }
  } else {
    if(ad->alarmState > 1) {
      ad->alarmState = ad->alarmState - 1;
      dlog_print(DLOG_WARN, LOG_TAG, "### alarmState decreased by one to %i", ad->alarmState);
    } else if (ad->alarmState == 1) {
      ad->alarmState = ad->alarmState - 1;
      dlog_print(DLOG_WARN, LOG_TAG, "### alarmState decreased by one to %i", ad->alarmState);
    }
  }

  if(ad->logging) {
    //write log to local storage after every analysis
    save_log(ad);
  }
}

//sensor event callback implementation, to be called every time sensor data is received!
void sensor_event_callback(sensor_h sensor, sensor_event_s *event, void *data)
{
  // Extracting application data
  appdata_s* ad = (appdata_s*)data;

  sensor_type_e type = SENSOR_ALL;

  if((sensor_get_type(sensor, &type) == SENSOR_ERROR_NONE) && type == SENSOR_LINEAR_ACCELERATION)
  {
    ringbuf_push(ad->rb_x, event->values[0]);
    ringbuf_push(ad->rb_y, event->values[1]);
    ringbuf_push(ad->rb_z, event->values[2]);

    //each second perform fft analysis -> a second has passed if sampleRate number of new entries were made in rb_X, i.e., if rb_x->idx % sampleRate == 0
    if((ad->rb_x)->idx % sampleRate == 0)
    {
      seizure_detection(ad);
    }
  }
}

//called on service app creation (initialized fft_trafos, rbs, and req params to defaults (these are overidden when started through UI!))
bool service_app_create(void *data)
{
  // Extracting application data
  appdata_s* ad = (appdata_s*)data;

  bool sensor_supported = false;
  if (sensor_is_supported(SENSOR_LINEAR_ACCELERATION, &sensor_supported) != SENSOR_ERROR_NONE || sensor_supported == false)
  {
    dlog_print(DLOG_ERROR, LOG_TAG, "Accelerometer not supported! Service is useless, exiting...");
    //TODO notify user over UI?!
    service_app_exit();
    return false;
  }

  ad->listener = NULL; //make sure that at startup listener is NULL, i.e., isRunning indicates that listener is not registered!

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

  ad->logging = false;

  //create notification handles
  //shutdown notification:
  ad->shutdown_notification = notification_create(NOTIFICATION_TYPE_NOTI);
  if (ad->shutdown_notification == NULL) dlog_print(DLOG_ERROR, LOG_TAG, "could not create shutdown notification!");
  int noti_err = NOTIFICATION_ERROR_NONE;
  noti_err = notification_set_text(ad->shutdown_notification, NOTIFICATION_TEXT_TYPE_TITLE, "Warning!", "EPILARM_NOTIFICATION_TITLE", NOTIFICATION_VARIABLE_TYPE_NONE);
  if (noti_err != NOTIFICATION_ERROR_NONE) dlog_print(DLOG_ERROR, LOG_TAG, "could not create title of shutdown notification!");
  noti_err = notification_set_text(ad->shutdown_notification, NOTIFICATION_TEXT_TYPE_CONTENT, "Unscheduled shutdown of Epilarm!\n (Restart via Epilarm app!)",
      "EPILARM_NOTIFICATION_CONTENT", NOTIFICATION_VARIABLE_TYPE_NONE);
  if (noti_err != NOTIFICATION_ERROR_NONE) dlog_print(DLOG_ERROR, LOG_TAG, "could not create content of shutdown notification!");
  noti_err = notification_set_vibration(ad->shutdown_notification, NOTIFICATION_VIBRATION_TYPE_DEFAULT, NULL);
  if (noti_err != NOTIFICATION_ERROR_NONE) dlog_print(DLOG_ERROR, LOG_TAG, "could not set vibration of shutdown notification!");

  //alarm notification:
  noti_err = notification_clone(ad->shutdown_notification, &(ad->alarm_notification));
  if (noti_err != NOTIFICATION_ERROR_NONE) dlog_print(DLOG_ERROR, LOG_TAG, "could not clone alarm notification!");
  noti_err = notification_set_text(ad->alarm_notification, NOTIFICATION_TEXT_TYPE_TITLE, "ALARM!", "EPILARM_NOTIFICATION_TITLE", NOTIFICATION_VARIABLE_TYPE_NONE);
  if (noti_err != NOTIFICATION_ERROR_NONE) dlog_print(DLOG_ERROR, LOG_TAG, "could not create title of alarm notification!");
  noti_err = notification_set_text(ad->alarm_notification, NOTIFICATION_TEXT_TYPE_CONTENT, "Seizure-like movements detected! (alarmState = -1)",
      "EPILARM_NOTIFICATION_CONTENT", NOTIFICATION_VARIABLE_TYPE_NONE);
  if (noti_err != NOTIFICATION_ERROR_NONE) dlog_print(DLOG_ERROR, LOG_TAG, "could not create content of alarm notification!");

  //warn notification:
  noti_err = notification_clone(ad->shutdown_notification, &(ad->warn_notification));
  ad->alarm_notification = notification_create(NOTIFICATION_TYPE_NOTI);
  if (noti_err != NOTIFICATION_ERROR_NONE) dlog_print(DLOG_ERROR, LOG_TAG, "could not clone warn notification!");
  noti_err = notification_set_text(ad->warn_notification, NOTIFICATION_TEXT_TYPE_TITLE, "WARNING!", "EPILARM_NOTIFICATION_TITLE", NOTIFICATION_VARIABLE_TYPE_NONE);
  if (noti_err != NOTIFICATION_ERROR_NONE) dlog_print(DLOG_WARN, LOG_TAG, "could not create title of warning notification!");

  return true;
}

//checks whether analysis is running, by checking if sensor listener is running
int is_running(void *data)
{
  // Extracting application data
  appdata_s* ad = (appdata_s*)data;

  return ad->listener != NULL;
}

//starts the sensor with the seizure detection algorithm, if data->logging is set to true after every analysis a log-file is saved.
void sensor_start(void *data)
{
  // Extracting application data
  appdata_s* ad = (appdata_s*)data;

  // Preparing and starting the sensor listener for the accelerometer.
  if (sensor_get_default_sensor(SENSOR_LINEAR_ACCELERATION, &(ad->sensor)) == SENSOR_ERROR_NONE)
  {
    if (sensor_create_listener(ad->sensor, &(ad->listener)) == SENSOR_ERROR_NONE
      && sensor_listener_set_event_cb(ad->listener, dataQueryInterval, sensor_event_callback, ad) == SENSOR_ERROR_NONE
      && sensor_listener_set_option(ad->listener, SENSOR_OPTION_ALWAYS_ON) == SENSOR_ERROR_NONE
      && sensor_listener_set_attribute_int(ad->listener, SENSOR_ATTRIBUTE_PAUSE_POLICY, SENSOR_PAUSE_NONE) == SENSOR_ERROR_NONE
      && device_power_request_lock(POWER_LOCK_CPU, 0) == DEVICE_ERROR_NONE)
    {
      if (sensor_listener_start(ad->listener) == SENSOR_ERROR_NONE)
      {
        dlog_print(DLOG_INFO, LOG_TAG, "Sensor listener started.");
        //create folder for logs
        if(ad->logging) {
          char log_path[MAX_PATH];
          get_log_path(log_path);
          mkdir(log_path, 0700);
        }
      }
    }
  }
}

//stops the sensor and the seizure detection algorithm.
//input: appdata, bool that tells if a warning notification should be sent (e.g. if it the sensor is not stopped by UI)
void sensor_stop(void *data, bool warn)
{
  // Extracting application data
  appdata_s* ad = (appdata_s*)data;

  if(!is_running(ad)) {
    dlog_print(DLOG_INFO, LOG_TAG, "Sensor listener already destroyed.");
    return;
  }

  //Stopping & destroying sensor listener
  if ((sensor_listener_stop(ad->listener) == SENSOR_ERROR_NONE)
    && (sensor_destroy_listener(ad->listener) == SENSOR_ERROR_NONE))
  {
    ad->listener = NULL;
    dlog_print(DLOG_INFO, LOG_TAG, "Sensor listener destroyed.");
    if (warn) { //send notification that sensorlistener is destroyed
      dlog_print(DLOG_INFO, LOG_TAG, "Unscheduled shutdown of Epilarm-service! (Restart via UI!)");
      publish_notification(ad->shutdown_notification);
      //TODO start UI and tell it that service app crashed!
      start_UI();
    }
  }
  else
  {
    dlog_print(DLOG_INFO, LOG_TAG, "Error occurred when destroying sensor listener: listener was never created!");
  }

  if(device_power_release_lock(POWER_LOCK_CPU) != DEVICE_ERROR_NONE) {
    dlog_print(DLOG_INFO, LOG_TAG, "could not release cpu lock!");
  }

}

//called when service app is terminated (this is NOT called when sensor listener is destroyed and analysis stopped!!)
void service_app_terminate(void *data)
{
  // Extracting application data
  appdata_s* ad = (appdata_s*)data;

  sensor_stop(data, true); //if sensor is stopped - with a warning; if already stopped before, no warning

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

  //free notification handles
  int noti_err = notification_free(ad->shutdown_notification);
  if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_WARN, LOG_TAG, "could not free shutdown notification!"); return; }
  noti_err = notification_free(ad->warn_notification);
  if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_WARN, LOG_TAG, "could not free warn notification!"); return; }
  noti_err = notification_free(ad->alarm_notification);
  if (noti_err != NOTIFICATION_ERROR_NONE) { dlog_print(DLOG_WARN, LOG_TAG, "could not free alarm notification!"); return; }

}

//handles incoming appcontrols from UI (e.g. starting/stopping analysis, ftp upload, compression, etc..)
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

      //perform adequate actions
        if((caller_id != NULL) && (action_value != NULL)
             && (!strncmp(caller_id, MYSERVICELAUNCHER_APP_ID, STRNCMP_LIMIT))
             && (!strncmp(action_value, "start", STRNCMP_LIMIT)))
        {
          //// >>>> START SERVICE APP <<<< ////
          //get params from appcontrol: [minFreq, maxFreq, avgRoiThresh, multThresh, warnTime, logging]
            dlog_print(DLOG_INFO, LOG_TAG, "Epilarm start! reading params...");
            char **params; int length;
          if (app_control_get_extra_data_array(app_control, "params", &params, &length) == APP_CONTROL_ERROR_NONE)
          {
            if (length != 6) { dlog_print(DLOG_INFO, LOG_TAG, "received too few params!"); }
            dlog_print(DLOG_INFO, LOG_TAG, "received %i params: minFreq=%s, maxFreq=%s, avgRoiThresh=%s, multThresh=%s, warnTime=%s, logging=%s",
                length, params[0], params[1], params[2], params[3], params[4], params[5]);
              char *eptr;
            //set new params
            ad->minFreq = strtod(params[0],&eptr);
            ad->maxFreq = strtod(params[1],&eptr);
            ad->avgRoiThresh = strtod(params[2],&eptr);
            ad->multThresh = strtod(params[3],&eptr);
            ad->warnTime = atoi(params[4]);
            ad->logging = atoi(params[5]);

            if(!is_running(ad)) {
              dlog_print(DLOG_INFO, LOG_TAG, "Starting epilarm sensor service!");
              sensor_start(ad); //TODO add return value in case starting of sensor listener fails!
            } else {
              dlog_print(DLOG_INFO, LOG_TAG, "Service already running! Not started again!");
            }
          } else {
            dlog_print(DLOG_INFO, LOG_TAG, "receiving params failed! sensor not started!");
          }
          free(params);

        } else if((caller_id != NULL) && (action_value != NULL)
             && (!strncmp(caller_id, MYSERVICELAUNCHER_APP_ID, STRNCMP_LIMIT))
             && (!strncmp(action_value, "stop", STRNCMP_LIMIT)))
        {
          //// >>>> STOP SERVICE APP <<<< ////
            dlog_print(DLOG_INFO, LOG_TAG, "Stopping epilarm sensor service!");
            sensor_stop(data, false); //stop sensor listener without notification (as it was shut down on purpose)

        } else if((caller_id != NULL) && (action_value != NULL)
                && (!strncmp(caller_id, MYSERVICELAUNCHER_APP_ID, STRNCMP_LIMIT))
                && (!strncmp(action_value, "running?", STRNCMP_LIMIT)))
        {
          //// >>>> CHECK IF ANALYSIS IS RUNNING <<<< ////
            dlog_print(DLOG_INFO, LOG_TAG, "are we running? (asked by UI)!");

          char *app_id;
          app_control_h reply;
        app_control_create(&reply);
        app_control_get_app_id(app_control, &app_id);
        app_control_add_extra_data(reply, APP_CONTROL_DATA_SELECTED, is_running(ad) ? "1" : "0");
        app_control_reply_to_launch_request(reply, app_control, APP_CONTROL_RESULT_SUCCEEDED);
            dlog_print(DLOG_INFO, LOG_TAG, "reply sent (%d)", is_running(ad));

        app_control_destroy(reply);

        free(app_id);

        } else if((caller_id != NULL) && (action_value != NULL)
                && (!strncmp(caller_id, MYSERVICELAUNCHER_APP_ID, STRNCMP_LIMIT))
                && (!strncmp(action_value, "compress_logs", STRNCMP_LIMIT)))
        {
          //// >>>> COMPRESS ALL LOGS <<<< ////
          dlog_print(DLOG_INFO, LOG_TAG, "Compressing logs!");
          int success = compress_logs(); //compress all locally stored logs
          //tell UI if compression failed
          char *app_id;
             app_control_h reply;
             app_control_create(&reply);
             app_control_get_app_id(app_control, &app_id);
             app_control_reply_to_launch_request(reply, app_control, (success==0) ? APP_CONTROL_RESULT_SUCCEEDED : APP_CONTROL_RESULT_FAILED);
            app_control_destroy(reply);
             free(app_id);
            dlog_print(DLOG_INFO, LOG_TAG, "reply sent (%d)", success==0);

        } else if((caller_id != NULL) && (action_value != NULL)
                && (!strncmp(caller_id, MYSERVICELAUNCHER_APP_ID, STRNCMP_LIMIT))
                && (!strncmp(action_value, "delete_logs", STRNCMP_LIMIT)))
        {
          //// >>>> DELETE ALL LOGS <<<< ////
          dlog_print(DLOG_INFO, LOG_TAG, "Deleting logs!");
          int success = delete_logs(); //compress all locally stored logs
          //tell UI if compression failed
          char *app_id;
             app_control_h reply;
             app_control_create(&reply);
             app_control_get_app_id(app_control, &app_id);
             app_control_reply_to_launch_request(reply, app_control, (success==0) ? APP_CONTROL_RESULT_SUCCEEDED : APP_CONTROL_RESULT_FAILED);
            app_control_destroy(reply);
             free(app_id);
            dlog_print(DLOG_INFO, LOG_TAG, "reply sent (%d)", success==0);

        } else if((caller_id != NULL) && (action_value != NULL)
                && (!strncmp(caller_id, MYSERVICELAUNCHER_APP_ID, STRNCMP_LIMIT))
                && (!strncmp(action_value, "log_upload", STRNCMP_LIMIT)))
        {
          //// >>>> UPLOAD COMPRESSED LOG FILES <<<< ////
             //get params from appcontrol: [ftp_hostname, ftpport, ftpusernam, ftppassword, ftppath]
            dlog_print(DLOG_INFO, LOG_TAG, "started epilarm log upload! reading params...");
            char **params; int length;
            if (app_control_get_extra_data_array(app_control, "params", &params, &length) == APP_CONTROL_ERROR_NONE)
            {
              if (length != 5) { dlog_print(DLOG_INFO, LOG_TAG, "received too few params!"); }
              dlog_print(DLOG_INFO, LOG_TAG, "received %i params: ftpHostname=%s, ftpPort=%s, ftpUsername=%s, ftpPassword=%s, ftpPath=%s",
                  length, params[0], params[1], params[2], params[3], params[4]);
              //set new params
              dlog_print(DLOG_INFO, LOG_TAG, "Start sharing of logs!");
                 char ftp_url[MAX_URL_LEN];
              //assemble ftp url
              strcpy(ftp_url, "ftp://");
              strcat(ftp_url, params[2]); //username
              strcat(ftp_url, ":");
              strcat(ftp_url, params[3]); //password
              strcat(ftp_url, "@");
              strcat(ftp_url, params[0]); //hostname
              strcat(ftp_url, ":");
              strcat(ftp_url, params[1]); //port
              strcat(ftp_url, "/");
              strcat(ftp_url, params[4]); //path
              strcat(ftp_url, "/");

              dlog_print(DLOG_INFO, LOG_TAG, "ftp-url is %s.", ftp_url);
              dlog_print(DLOG_INFO, LOG_TAG, "Starting upload to ftp server!");
              int success = share_data(ftp_url);
              if (success==-1) {
                dlog_print(DLOG_INFO, LOG_TAG, "upload failed!");
              } else {
                dlog_print(DLOG_INFO, LOG_TAG, "upload finished!");
              }

              char *app_id;
              app_control_h reply;
              app_control_create(&reply);
              app_control_get_app_id(app_control, &app_id);
              app_control_reply_to_launch_request(reply, app_control, (success==0) ? APP_CONTROL_RESULT_SUCCEEDED : APP_CONTROL_RESULT_FAILED);
              dlog_print(DLOG_INFO, LOG_TAG, "reply sent (%d)", success==0);

              app_control_destroy(reply);
              free(app_id);
            } else {
              dlog_print(DLOG_INFO, LOG_TAG, "receiving params failed! no logs shared!");
            }
            free(params);
        } else {
            dlog_print(DLOG_INFO, LOG_TAG, "Unsupported action! Doing nothing...");
        }

        free(caller_id);
        free(action_value);
        return;
    }
}

//called when language is changed (??)
static void service_app_lang_changed(app_event_info_h event_info, void *user_data)
{
  /*APP_EVENT_LANGUAGE_CHANGED*/
  return;
}

//called when ???
static void service_app_region_changed(app_event_info_h event_info, void *user_data)
{
  /*APP_EVENT_REGION_FORMAT_CHANGED*/
}

//called when battery is low => do nothing TODO should we do sth?!
static void service_app_low_battery(app_event_info_h event_info, void *user_data)
{
  /*APP_EVENT_LOW_BATTERY*/
}

//called when memory is low => stop logging if it was activated!
static void service_app_low_memory(app_event_info_h event_info, void *user_data)
{
  // Extracting application data
  appdata_s* ad = (appdata_s*)user_data;

  /*APP_EVENT_LOW_MEMORY*/
  ad->logging=FALSE;
  //TODO notify UI that logging was disabled!
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
