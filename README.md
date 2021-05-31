

<img align="left" width="100" height="100" src="https://github.com/j-danner/Epilarm/blob/master/Epilarm_web_app/icon.png" alt="Epilarm app icon">

# Epilarm


Detection of generalized tonic-clonic seizures using a _Galaxy Watch Active 2_.

The general framework is heavily inspired by the project [OpenSeizureDetector](https://github.com/OpenSeizureDetector) which has the same goal but targets Garmin, Pebble, and (recently) PineTime smartwatches.
This project targets the use of the smartwatch **Galaxy Watch Active 2**, which - in my opinion - looks nicer on your wrist and offers slightly more functionality ;)

(The project's main goal is to give my girlfriend a little more freedom.)

__UPDATE:__ ___Before starting to write the companion Android app, I took another look (and contacted the creator of) the afore-mentioned [OpenSeizureDetector](https://github.com/OpenSeizureDetector), which already has a (reliable) Android app for the analysis of movement data and notification via SMS when a seizure is suspected. 
In order to avoid a second (independent) Android app that _only_ supports Tizen devices, and that can only send a emergency notification; I decided to switch to their approach and the OSD-app. That means instead of doing the 'hard' analysis on the watch and writing my own Android app, Epilarm will now also send raw acceleration data directly to the OSD-app (running on an Android phone) via Bluetooth LE, and let the app do all the 'hard' work.
Due to the notably different appraoch compared to this project where the watch does _everything_ on its own, the OSD-client for Tizen OS can be found in a different [repo](https://github.com/j-danner/OpenSeizureDetector_Tizen). In case the battery consumption and seizure detection rate prove to be comparably good, the stand-alone 'Epilarm' will certainly be discontinued.___


## Methodology
In order to detect seizures we analyse the linear accelerometer data of the watch to filter out frequencies that correspond to (or are likely to appear during) epileptic seizures.
By default, the accelerometer data is collected every 20ms and every second the last 10 seconds of data are analyzed. This way we have a frequency resolution of 0.1Hz and the highest frequency we can detect is 25Hz.
(The GTCS at hand seem to admit frequencies in the range 2.5Hz--7.5Hz, which we call the region of interest - or short _ROI_.

Now every second an internal variable (`alarmstate`) is increased by one if two conditions are satisfied:
1. the avg magnitude of the freqs in the ROI are higher by a given factor (`multThresh`) than the avg of the remaining freqs, and
2. the avg magnitude of the freqs in the ROI is higher than a given value (`avgThresh`).
Otherwise it is decreased by one (as long as it is positive).

As soon as this variable reaches a certain value (`warnTime`), an alarm is raised and one or more contact persons are notified by a phone call or SMS including the current GPS coordinates of the watch. (__NOTIFICATIONS NOT YET IMPLEMENTED__).


These parameters, including the bounds of the ROI, can be chosen in the UI application. In order to optimize the parameters to your seizures, activate the logging option and upload them to a remote FTP-server every now and then. If you have collected enough data, the python-script [log_analysis.py](log_analysis/log_analysis.py) may be of help. (__SCRIPT NOT YET FINISHED__)


## Current state
As of now, NO notifications are sent to your phone or predefined phone numbers, the notifications are only local to your watch. (This will change as soon as I have confirmation that seizures can be reliably detected with this approach, and I have confirmation that it is not possible to use Samsung's very own _SOS feature_ for that.)

More information on the current state, future plans and other todos can be found [here](current_state.md).


## Libraries/Files used in this project
 - [c-ringbuf](https://github.com/dhess/c-ringbuf) for (fast) ringbuffers in c (removed all unnecessary parts)
 - [fft-c](https://github.com/adis300/fft-c) for FFT computation, based on [FFTPack](http://www.netlib.org/fftpack/)
 - [MicroTar](https://github.com/rxi/microtar) for tarring of logs before compression (this [PR](https://github.com/byronhe/microtar) is used)
 - standard C-libraries and a few included in the [Tizen API](https://docs.tizen.org/application/native/api/wearable/5.5/group__CAPI__BASE__FRAMEWORK.html) (AFAIR [glibc](http://www.gnu.org/software/libc/) and [zlib](http://www.zlib.net/)).

Special thanks also go out to _Graham Jones_ of [OpenSeizureDetector](https://github.com/OpenSeizureDetector) for his good and detailed explanations ([technical details](http://openseizuredetector.github.io/OpenSeizureDetector/meta/2015/02/01/Pebble_Watch_Version/) and [general overview](https://github.com/OpenSeizureDetector/Presentations/blob/master/01_CfAI_Seminar_Aug2020_Issue_1.pdf)).
