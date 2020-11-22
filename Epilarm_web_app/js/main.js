//parameters, must be initialized by reset_params() or loaded from localstorage. Simply use load_params() once!
var params;

var SERVICE_APP_ID = tizen.application.getCurrentApplication().appInfo.packageId + '.epilarm_sensor_service'; //'QOeM6aBGp0.epilarm_sensor_service';
var APP_ID = tizen.application.getCurrentApplication().appInfo.id;

//save params to local storage, must be called after changing any value
function save_params() {
    // Set the local storage
    if ('localStorage' in window) {
        //save params
        localStorage.setItem('params', JSON.stringify(params));
        console.log('params saved!');
    } else {
        console.log('save_params: no localStorage in window!');
    }
}

//must be called on startup of UI
function load_params() {
    if ('localStorage' in window) {
        // params that can be changed
        if (localStorage.getItem('params') === null) {
            console.log('load_params: no saved params found, using default values!');
            //use default values!
            reset_params();
        } else {
            params = JSON.parse(localStorage.getItem('params'));
            console.log('params loaded!');
        }
    } else {
        console.log('load_params: no localStorage in window found, using default values!');
        reset_params();
    }
}

//start compression of logs and upload of those to ftp server 
function start_ftp_upload() {
	//make sure screen stays on!
	tizen.power.request('SCREEN', 'SCREEN_NORMAL');
	
	console.log('ftp_upload: checking if wifi is on... TODO'); //TODO
	//TODO automatically switch on wifi if necessary...
	/*if(navigator.connection.type != Connection.WIFI) {
		console.log("ftp_upload: wifi is on, ftp upload can be started!");
	} else {
		//switch wifi on... 
		
	}*/
	//start data compression!
    var obj = new tizen.ApplicationControlData('service_action', ['compress_logs']); //you'll find the app id in config.xml file.
    var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj]
    );
    var appControlReplyCallback = {
    		// callee sent a reply
            onsuccess: function(data) {
                console.log('ftp_upload: compression done.');
                //end loading symbol...
                tau.closePopup();
                
            	//compression of logs is done, start ftp-upload now
                console.log('ftp_upload: starting service app for ftp upload...');
                var obj = new tizen.ApplicationControlData('service_action', ['log_upload']);
                var obj_params = new tizen.ApplicationControlData('params', params.ftpToString());
                var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
                    null,
                    null,
                    null, [obj, obj_params]
                );
                var appControlReplyCallback = {
                    // callee sent a reply
                    onsuccess: function(data) {
                        console.log('ftp_upload: ftp-upload done.');
                        //end loading symbol...
                        tau.closePopup();
                    },
                    // callee returned failure
                    onfailure: function() {
                        console.log('ftp_upload: ftp-upload failed.');
                        //end loading popup
                        tau.closePopup();
                        //show upload failed popup
                        tau.openPopup('#UploadFailedPopup');
                    }
                };
                try {
                    tizen.application.launchAppControl(obj1,
                        SERVICE_APP_ID,
                        function() {
                            console.log('ftp_upload: Log upload starting succeeded');
                            //update text in popup
                            document.getElementById('UploadPopupText').innerHTML = 'uploading logs...';
                        },
                        function(e) {
                            console.log('ftp_upload: Log upload starting failed : ' + e.message);
                            tau.closePopup();
                        }, appControlReplyCallback);
                } catch (e) {
                    window.alert('ftp_upload: Error when starting appcontrol! error msg:' + e.toString());
                }
                //reset popup text
                document.getElementById('UploadPopupText').innerHTML = 'compressing logs...';
                
                //make screen turn off again:
            	tizen.power.release('SCREEN');
            },
            // callee returned failure
            onfailure: function() {
                console.log('ftp_upload: compression failed');
                //end loading popup
                tau.closePopup();
                //show upload failed popup
                tau.openPopup('#CompressionFailedPopup');
                
                //make screen turn off again:
            	tizen.power.release('SCREEN');
            }
        };
    try {
        tizen.application.launchAppControl(obj1,
            SERVICE_APP_ID,
            function() {
                console.log('ftp_upload: Starting compression of logs succeeded.');
            },
            function(e) {
                console.log('ftp_upload: Starting compression of logs failed : ' + e.message);
            }, appControlReplyCallback);
    } catch (e) {
        window.alert('ftp_upload: Error when starting appcontrol for compressing logs! error msg:' + e.toString());
    }
}

//start service app and seizure detection
function start_service_app() {
    console.log('starting service app...');
    var obj = new tizen.ApplicationControlData('service_action', ['start']);
    var obj_params = new tizen.ApplicationControlData('params', params.analysisToString());
    var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj, obj_params]
    );
    try {
        tizen.application.launchAppControl(obj1,
            SERVICE_APP_ID,
            function() {
                console.log('Launch Service succeeded');
            },
            function(e) {
                console.log('Launch Service failed : ' + e.message);
                tau.openPopup('#StartFailedPopup');
            }, null);
    } catch (e) {
        window.alert('Error when starting appcontrol for starting seizure detection! error msg:' + e.toString());
    }
}

//stop service app and seizure detection
function stop_service_app() {
    console.log('stopping service app...');
    var obj = new tizen.ApplicationControlData('service_action', ['stop']); //you'll find the app id in config.xml file.
    var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj]
    );
    try {
        tizen.application.launchAppControl(obj1,
            SERVICE_APP_ID,
            function() {
                console.log('Stopping Service Request succeeded');
            },
            function(e) {
                console.log('Stopping Service Request failed : ' + e.message);
            }, null);
    } catch (e) {
        window.alert('Error when starting appcontrol for stopping seizure detection! error msg:' + e.toString());
    }
}

//update seizure detection, logging and alarm

function update_checkboxes() {
    console.log('ask service app if the sensor listener is running...');
    var obj = new tizen.ApplicationControlData('service_action', ['running?']); //you'll find the app id in config.xml file.
    var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj]
    );
    var appControlReplyCallback = {
        // callee sent a reply
        onsuccess: function(data) {
            //update checkbox!
            document.getElementById('start_stop_checkbox').checked = (data[0].value[0] === '1');
            console.log('received: ' + data[0].value[0]);
            console.log('updated checkbox! (to ' + (data[0].value[0] === '1') + ')');
            
            //change from loading page to mainpage
            tau.changePage('#main');
        },
        // callee returned failure
        onfailure: function() {
            console.log('reply failed');
            
            //change from loading page to mainpage
            tau.changePage('#main');
            //create warning for user!
            tau.openPopup('#RunningFailedPopup');
        }
    };
    try {
        tizen.application.launchAppControl(obj1,
            SERVICE_APP_ID,
            function() {
                console.log('Checking whether sensor listener is running succeeded');
            },
            function(e) {
                console.log('Checking whether sensor listener is running failed : ' + e.message);
            },
            appControlReplyCallback);
    } catch (e) {
        window.alert('Error when starting appcontrol to detect whether seizure detection is running! error msg:' + e.toString());
    }
    
    //update logging and alarm checkbox
    console.log('updating logging checkbox');
    var logging_box = document.querySelector('#logging_checkbox');
    logging_box.checked = params.logging;
    //logging_box.addEventListener('load', listener, useCapture)
    console.log('updating alarm checkbox');
    var alarm_box = document.querySelector('#alarm_checkbox');
    alarm_box.checked = params.alarm;
}

/*
function toggle_settings_visibility() {
	 var setting_elems = document.querySelectorAll('*[id^="settings_"]');
	 
	 for(elem in setting_elems) {
		 if (elem.style.opacity < 1.0) {
			 elem.style.opacity = 1.0;
			 elem.style.click(true);
		 } else {
			 elem.style.opacity = 0.5;
			 elem.style.click(false);
		 }
	 }
}*/



function start_stop(id) {
    if (document.getElementById(id).checked) {
        start_service_app();
        //toggle_settings_visibility();
    } else {
        stop_service_app();
        //toggle_settings_visibility();
    }
}


function toggle_logging(id) {
    if (document.getElementById(id).checked) {
        params.logging = true;
    } else {
        params.logging = false;
    }
    save_params();
}

/*
//this function directly calls the applicationcontrols that start the seizure detection of sensor-service.
//due to the fact that (for some non-understandable reason) the API allows only appcontrols of UI applications, we cannot use this :(
//instead we call the UI itself s.t. it starts/stops the service for us!
function add_alarms() {
	console.log('add_alarm: start.');
	remove_alarms();
	console.log('add_alarm: alarms removed.');

	//generate appcontrol to be called on remove_alarm
    var obj_stop = new tizen.ApplicationControlData('service_action', ['stop']); //you'll find the app id in config.xml file.
    var appcontrol_stop = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj_stop]
    );
   
    var obj_start = new tizen.ApplicationControlData('service_action', ['start']);
    var obj_params_start = new tizen.ApplicationControlData('params', params.analysisToString());
    var appcontrol_start = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj_start, obj_params_start]
    );
	console.log('add_alarm: created application controls.');
	
	var notification_content_stop =
	{
	  content: 'Automatically started seizure detection!',
	  actions: {vibration: true}
	};
	var notification_stop = new tizen.UserNotification('SIMPLE', 'Epilarm', notification_content_stop);

	var notification_content_start =
	{
	  content: 'Automatically stopped seizure detection!',
	  actions: {vibration: true}
	  //actions: {soundPath: "music/Over the horizon.mp3", vibration: true}
	};
	var notification_start = new tizen.UserNotification('SIMPLE', 'Epilarm', notification_content_start);
	console.log('add_alarm: created notifications.');

	var alarm_notification_start = new tizen.AlarmAbsolute(params.alarm_start_date);//, params.alarm_days);
	var alarm_notification_stop = new tizen.AlarmAbsolute(params.alarm_stop_date);//, params.alarm_days);
	var alarm_start = new tizen.AlarmAbsolute(params.alarm_start_date);//, params.alarm_days);
	var alarm_stop = new tizen.AlarmAbsolute(params.alarm_stop_date);//, params.alarm_days);
	console.log('add_alarm: created alarms.');
	
	tizen.alarm.addAlarmNotification(alarm_notification_start, notification_start);
	tizen.alarm.addAlarmNotification(alarm_notification_stop, notification_stop);
	console.log('add_alarm: scheduled notifications.');

	tizen.alarm.add(alarm_stop, SERVICE_APP_ID); //, appcontrol_stop);
	console.log('add_alarm: scheduled stop appcontrol.');
	tizen.alarm.add(alarm_start, SERVICE_APP_ID, appcontrol_start);
	console.log('add_alarm: scheduled start appcontrol.');

	console.log('add_alarm: scheduled appcontrols.');

	console.log('add_alarm: done.');
}
*/

function add_alarms() {
	console.log('add_alarm: start.');
	remove_alarms();
	console.log('add_alarm: alarms removed.');

	//generate appcontrol to be called on remove_alarm
    var obj_stop = new tizen.ApplicationControlData('service_action', ['stop']); //you'll find the app id in config.xml file.
    var appcontrol_stop = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj_stop]
    );
   
    var obj_start = new tizen.ApplicationControlData('service_action', ['start']);
    var appcontrol_start = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj_start]
    );
	console.log('add_alarm: created application controls.');
	
	var notification_content_stop =
	{
	  content: 'Automatically started seizure detection!',
	  actions: {vibration: true}
	};
	var notification_stop = new tizen.UserNotification('SIMPLE', 'Epilarm', notification_content_stop);

	var notification_content_start =
	{
	  content: 'Automatically stopped seizure detection!',
	  actions: {vibration: true}
	  //actions: {soundPath: "music/Over the horizon.mp3", vibration: true}
	};
	var notification_start = new tizen.UserNotification('SIMPLE', 'Epilarm', notification_content_start);
	console.log('add_alarm: created notifications.');

	var alarm_notification_start = new tizen.AlarmAbsolute(params.alarm_start_date);//, params.alarm_days);
	var alarm_notification_stop = new tizen.AlarmAbsolute(params.alarm_stop_date);//, params.alarm_days);
	var alarm_start = new tizen.AlarmAbsolute(params.alarm_start_date);//, params.alarm_days);
	var alarm_stop = new tizen.AlarmAbsolute(params.alarm_stop_date);//, params.alarm_days);
	console.log('add_alarm: created alarms.');
	
	tizen.alarm.addAlarmNotification(alarm_notification_start, notification_start);
	tizen.alarm.addAlarmNotification(alarm_notification_stop, notification_stop);
	console.log('add_alarm: scheduled notifications.');

	tizen.alarm.add(alarm_stop, APP_ID); //, appcontrol_stop);
	console.log('add_alarm: scheduled stop appcontrol.');
	tizen.alarm.add(alarm_start, APP_ID, appcontrol_start);
	console.log('add_alarm: scheduled start appcontrol.');

	console.log('add_alarm: scheduled appcontrols.');

	console.log('add_alarm: done.');
}


//removes all scheduled alarms of app (i.e. all scheduled starting and stopping calls to service app)
function remove_alarms() {
	console.log('remove_alarm: start.');
	tizen.alarm.removeAll();
	console.log('remove_alarm: removed all scheduled alarms.');
	console.log('remove_alarm: done.');
}



function toggle_alarm(id) {
    if (document.getElementById(id).checked) {
        params.alarm = true;
        add_alarms();
    } else {
        params.alarm = false;
        remove_alarms();
    }
    save_params();
}



function reset_params() {
	//default values
    params = {
        minFreq: 3, //minimal freq of seizure-like movements (3Hz -> TODO verify using videos!)
        maxFreq: 8, //maximum freq of seizure-like movements (8Hz -> TODO verify using videos!)
        avgRoiThresh: 2.3, //average value above which the relevant (combined) freqs have to be for warning mode
        multThresh: 2.5, //threshhold for ration of average of non-relevant freqs and relevant freqs required for warning mode (as sum of the rations of the three dimensions)
        warnTime: 10, //time after which a continuous WARNING state raises an ALARM


        //variables that can be changed in settings:
        logging: true, //boolean to decide if sensordata should be stored
        ftpHostname: '192.168.178.33', //address of your (local) ftp server to which logs are uploaded
        ftpPort: '21', //port under which the broker is found
        ftpUsername: 'sam-gal-act-2-jul', //username
        ftpPassword: 'T5tUZKVKWq8FPAh5', //password
        ftpPath: 'share/epilarm/log/jul', //path on ftp server to store files in
        
        //alarm info
        alarm: false,
        alarm_start_date: new Date(Date.parse("2020-11-22T16:20")),
        alarm_stop_date: new Date(Date.parse("2020-11-22T16:30")),
        alarm_days: ["MO", "TU", "WE", "TH", "FR", "SA", "SU"],


        analysisToString: function() {
            return [this.minFreq.toString(), this.maxFreq.toString(), this.avgRoiThresh.toString(), this.multThresh.toString(), this.warnTime.toString(), (this.logging ? '1' : '0')];
        },
        ftpToString: function() {
            return [this.ftpHostname, this.ftpPort, this.ftpUsername, this.ftpPassword, this.ftpPath];
        }
    };
}



window.onload = function() {
    //leave screen on
    //tizen.power.request('SCREEN', 'SCREEN_NORMAL');

	//load user settings
    load_params();

    //make sure that checkboxes are in correct state on startup, also this changes to main page!
    update_checkboxes();

    window.addEventListener('appcontrol', function onAppControl() {
        var reqAppControl = tizen.application.getCurrentApplication.getRequestedAppControl();
        if (reqAppControl) {
            if (reqAppControl.appControl.data === 'start') {
            	start_sensor_service();
            } else if (reqAppConttrol.appControl.data === 'stop') {
				stop_service_app();
			}
        }
    });

    console.log('UI started!');
};