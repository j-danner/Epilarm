# Epilarm (for Galaxy Watch Active 2)
Detection of generalized tonic-clonic seizures using the linear accelerometer of a Galaxy Watch Active 2.

The general framework is heavily inspired by the project [OpenSeizureDetector](https://github.com/OpenSeizureDetector) which has the same goal but targets Garmin, Pebble, and PineTime smartwatches.
This project targets the use of the smartwatch **Galaxy Watch Active 2**, which - in my opinion - looks nicer on your wrist and offers slightly more functionality ;)

(The project's main goal is to give my girlfriend a little more freedom.)


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
As of now, NO notifications are sent to your phone or predefined phone numbers, the notifications are only local to your watch. (This will change as soon as I have confirmation that seizures can be reliably detected with this approach, and have confirmation that one cannot use Samsung's very own _SOS feature_ for that.)

More information on the current state, future plans and other todos can be found [here](current_state.md).


## Libraries/Files used in this project
 - [c-ringbuf](https://github.com/dhess/c-ringbuf) for fast ringbuffers in c
 - [fft-c](https://github.com/adis300/fft-c] for FFT computation, based on (FFTPack)[http://www.netlib.org/fftpack/)
 - [MicroTar](https://github.com/rxi/microtar) for tarring of logs before compression (to be precise we use the [PR](https://github.com/byronhe/microtar))

Special thanks also go out to _Graham Jones_ for his good and detailed explanations of his approach with [OpenSeizureDetector](https://github.com/OpenSeizureDetector) ([technical details][http://openseizuredetector.github.io/OpenSeizureDetector/meta/2015/02/01/Pebble_Watch_Version/] and [general overview][https://github.com/OpenSeizureDetector/Presentations/blob/master/01_CfAI_Seminar_Aug2020_Issue_1.pdf]).
