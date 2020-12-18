## Current State/TODO
The project is split into two parts. The first is a native service app (written in C) that runs _reliable_ in the background and takes care of the sensor data, analysis thereof and the notifications (in case a seizure is thought to be detected). The other is a web app - the UI - and is responsible for the settings, starting and stopping of the service app. Additionally, you can activate logging of the fft-output. After collecting some logs, they can be compressed and uploaded to a FTP server from the watch (WiFi needs to be truned on).

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
  - [ ] appcontrol to delete all log files
  - [ ] raise alarm by initializing phone call and/or send SMS (write companion app for Android or stick to LTE version of smart watch/can we use Samsung's very own SOS system?)
  - [ ] read GPS data every now and then s.t. it can be sent as an attachment of the alarm notification
  - [ ] automatically start and stop analysis at given times (use Alarm API?!)


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
  - [ ] deactivate settings if sensor listener is running (settings only get applied after restart of listener!)
  - [ ] button to delete all log files (with confirmation popup, then one can easily erase data each day if no seizure happened...)


- Python Log Analysis:
  - [x] download tars from ftp server
  - [x] convert json into python-dicts
  - [x] (automatic) analysis of 'gaps' between measurements
  - [ ] verify c-implementation, i.e., comptuation of multRatio, avgROI (etc) from the spectrum, then remove those values from the logs
  - [ ] using list of timestamps from actual seazures and their corresponding logs, optimize params of analysis s.t. less false alarms are raised but still all seizures detected


other todos:
 - [ ] fix default values on SM-R830; I could at least not trigger the alarm on purpose :/
 - [ ] optimize battery usage (currently 4.2%/h battery on SM-R820 with logging enabled)
  - with/without logging (try to minimize the difference, logging is (at least for now) extremely important!)
  - decrease sampleRate (beware of aliasing effects!)
 - [ ] check whether _night mode_ affects actual sampling rate
 - [x] make first prototype available to first _real_ test person (not just my shaky hand)
 - [ ] clarify which LICENSE can be used
 - [ ] create logo
