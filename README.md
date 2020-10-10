# Epilarm_GalaxyWatchActive2
Detection of epileptic seizures using a Galaxy Watch Active2.

Disclaimer: The idea for detecting seizures used here is very similar to that of the related project [OpenSeizureDetector](https://github.com/OpenSeizureDetector) which targets Garmin, Pebble, and PineTime smartwatches.
Whereas this project targets the use of the smartwatch **Galaxy Watch Active2**.

## Methodology
In order to detect seizures we analyse the accelerometer data of the watch to filter out frequencies that correspond to (or are likely to appear during) epileptic seizures.
By default, the accelerometer data is collected 32 times per second and analizes every second the last 8 seconds of data, this way we have a frequency resolution of 0.125Hz and the highest frequency we can detect is 16Hz. 
(The epileptic seizures at hand seem to admit frequencies in the range 2.5Hz--7.5Hz, which we call the region of interest (ROI).)

Now in each second we `alarmstate` is increased by one if 
 (1) the avg of the freqs in the ROI are higher by a factor of 2.5 than the remaining freqs, and
 (2) the avg of the freqs in the ROI are higher than 6.
Otherwise it is decreased by one (as long as it is positive). 
As soon as the alarmstate reaches 10, an alarm is raised and contact person(s) are notified by a phone call or SMS.


## Current State
As of now the following features are implemented:
- As a [Tizen Native Service Application](https://docs.tizen.org/application/native/guides/applications/service-app/):
  - collecting accelerometer data
  - analysis of collected data as described above
  - sending warnings of if alarmstate is increased as a local notification on the watch
 
- as a Tizen Web Application (javascript):
  - UI that allows starting and stopping of analysis of native service app


Todo:
 - automatic logging of analyzed data in case the alarmstate is increased (in order to later optimized default values)
 - implement raising of alarm, i.e. initialize phone call and/or send SMS (write companion app for Android or stick to LTE version of smart watch?)
 - read GPS data every now and then s.t. it can be send as an attachment of the alarm notification
 - generate UI that shows plot of freqs and allows to fully customize all parameters
 - generate widget that shows if the app and service are running correctly
 - clarify if RB.js can be used under MIT license!
 
