# Epilarm (for Galaxy Watch Active 2)
Detection of generalized tonic-clonic seizures using the linear accelerometer of a Galaxy Watch Active 2.

Disclaimer: The idea for detecting seizures used here is heavily inspired by the approach of the project [OpenSeizureDetector](https://github.com/OpenSeizureDetector) which has the same goal but targets Garmin, Pebble, and PineTime smartwatches.
This project targets the use of the smartwatch **Galaxy Watch Active 2** which offers more functionality and IMHO looks nicer on your wrist ;)

## Methodology
In order to detect seizures we analyse the linear accelerometer data of the watch to filter out frequencies that correspond to (or are likely to appear during) epileptic seizures.
By default, the accelerometer data is collected every 20ms and every second the last 10 seconds of data are analyzed. This way we have a frequency resolution of 0.1Hz and the highest frequency we can detect is 25Hz. 
(The GTCS at hand seem to admit frequencies in the range 2.5Hz--7.5Hz, which we call the region of interest - or short _ROI_.

Now every second the variable `alarmstate` is increased by one if
1. the avg magnitude of the freqs in the ROI are higher by a factor of 2.5 than the avg of the remaining freqs, and
2. the avg magnitude of the freqs in the ROI is higher than 2.3.
Otherwise it is decreased by one (as long as it is positive). 

As soon as the alarmstate reaches 10, an alarm is raised and one or more contact persons are notified by a phone call or SMS including the current GPS coordinates of the watch.


## Current State
The project is split into two parts. The first is a native service app (written in C) that runs reliable in the background and takes care of the sensor data, analysis thereof and the notifications. The other is a web app - the UI - and is responsible for the settings, starting and stopping of the service app. i.e., managing the seizure detection and some basic graphs. Additionally, we want to send logging data every now and then (via a button in the UI) to a (local) FTP server.

Below you can find a task-list which indicates the current state of the project and the goals for the near future:

- [Tizen Native Service Application](https://docs.tizen.org/application/native/guides/applications/service-app/):
  - [x] collecting of accelerometer data
  - [x] analysis of collected data as described above
  - [x] sending of notifactions if alarmstate is increased as a local notification on the watch
  - [x] send notification on shutdown (if not triggered by UI) to detect when it _crashes_
  - [x] complete logging of (analyzed) data (in order to later optimize default values); collect data for up to one week (timestamp, minFreq, maxFreq, complete simplified freqs, alarmstate, avg_roi_x|y|z, avg_nroi_x|y|z, avg_roi, multRatio --> 62 data points per second)
  - [x] implement data transfer to Raspberry Pi 4 (running home assistant) via FTP (to be triggered via button in UI)
  - [x] tar and gzip all log files before upload
  - [ ] optimize analysis, i.e., remove the (unnecessary) aggregation of the spectra in 0.5Hz bins
  - [x] implement basic functionality for ftp upload
  - [x] start UI (web app) if alarmstate changes (and app is not running)
  - [ ] raise alarm by initializing phone call and/or send SMS (write companion app for Android or stick to LTE version of smart watch/can we use Samsung's very own SOS system?)
  - [ ] read GPS data every now and then s.t. it can be sent as an attachment of the alarm notification 
 
 
- [Tizen Web Application](https://docs.tizen.org/application/web/index) (javascript):
  - [x] UI that allows starting and stopping of seizure detection of native service app
  - [x] basic setup of UI (one list, on the top switch to toggle analysis, below settings, last infos on app)
  - [x] check on start of UI if service app is running (and initialize button correctly!)
  - [x] button for sending collected internal data via FTP to specified IP address
  - [x] settings, sent to service app on startup of seizure detection:
   - warnTime, mult_thresh, roi_thresh, min|maxFreq (only in 0.5Hz steps)
  - [x] settings, sent to service app when compressing logs and uploading to FTP server:
   - hostname, port, username, password, path
  - [x] UI to change default values of params (see above)
  - [x] save settings to local storage and load them from there
  - [x] if something goes wrong in the service app, give appropriate popup warnings in UI (compression failed, upload failed, seizure detection failed)


- Python Log Analysis:
  - [ ] download tars from ftp server
  - [x] convert json into python-dicts
  - [x] (automatic) analysis of 'gaps' between measurements
  - [ ] verify c-implementation that multRatio, avgROI (etc) are computed correctly from the spectrum, then remove those from the logs
  - [ ] using list of timestamps from actual seazures and their corresponding logs, optimize params of analysis
  
other todos:
 - [ ] optimize battery usage (currently 4.2%/h battery on SM-R820 with logging enabled)
  - with/without logging (try to minimize the difference, logging is (at least for now) extremely important!)
  - decrease sampleRate (beware of aliasing effects!)
 - [x] make first prototype available to first _real_ test person (not just my shaky hand)
 - [ ] clarify which LICENSE can be used
 - [ ] create logo

 
