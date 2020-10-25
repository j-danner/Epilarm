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
The project is split into two parts. The first is a native service app (written in C) that runs reliable in the background and takes care of the sensor data, analysis thereof and the notifications. The other is a web app - the UI - and is responsible for the settings, starting and stopping of the service app. i.e., managing the seizure detection and some basic graphs. Additionally, we want to send logging data every now and then (via a button in the UI) to a local MQTT broker.

Below you can find a task-list which indicates the current state of the project and the goals for the near future:

- [Tizen Native Service Application](https://docs.tizen.org/application/native/guides/applications/service-app/):
  - [x] collecting of accelerometer data
  - [x] analysis of collected data as described above
  - [x] sending of notifactions if alarmstate is increased as a local notification on the watch
  - [x] send notification on shutdown (if not triggered by UI) to detect when it _crashes_
  - [ ] implement dataControl in service app for exchange of data with UI (data + settings)
  - [ ] complete logging of (analyzed) data (in order to later optimize default values); collect data for up to one week (timestamp, minFreq, maxFreq, complete simplified freqs, alarmstate, avg_roi_x|y|z, avg_nroi_x|y|z, avg_roi, multRatio --> 62 data points per second)
  - [ ] implement data transfer to Raspberry Pi 4 (running home assistant) via MQTT (triggered via button in UI)
  - [x] start UI (web app) if alarmstate changes (and app is not running)
  - [ ] raise alarm by initializing phone call and/or send SMS (write companion app for Android or stick to LTE version of smart watch/can we use Samsung's very own SOS system?)
  - [ ] read GPS data every now and then s.t. it can be sent as an attachment of the alarm notification 
 
- Tizen Web Application (javascript):
  - [x] UI that allows starting and stopping of analysis of native service app
  - [x] basic setup of UI (three pages: _settings_ - _on-off-switch_ - _graphs_)
  - [x] check on start of UI if service app is running (and initialize button correctly!)
  - [ ] receive data from dataControl of service app and plot freqs, multRatio, avg_roi for plotting
  - [ ] button for sending collected internal data via MQTT to specified IP address
  - [x] settings (that are sent to service app on startup):
   - **mandatory** warnTime, mult_thresh, roi_thresh, min|maxFreq (only in 0.5Hz steps)
   - **optional**  MQTT ip address 
   - **withWarning** dataAnalysisInterval, queryInterval, sampleRate (values are entangled + cannot be chosen arbitrarily + are not logged!)
  - [ ] UI to change default values of *mandatory*/*optional* settings (see above)
  - [ ] save settings to local storage
  - [ ] clarify whether RB.js can be used under MIT license! (is it actually used somewhere?!)


other todos:
 - [ ] generate widget that shows if the app and service are running correctly (?)
 - [ ] create logo
 - [ ] make first prototype available to first _real_ test person (not just my shaky hand)
 
