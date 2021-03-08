//parameters, must be initialized by reset_params() or loaded from localstorage. Simply use load_params() once!
var params;

//list items
var list;
//store references to html objects of items
var start_stop;
var start_stop_checkbox;

var logs_upload;
var logs_delete;

var automatic_analysis;
var automatic_analysis_checkbox;
//settings
var settings_alarm_start;
var settings_alarm_stop;
var settings_minfreq;
var settings_maxfreq;
var settings_avgthresh;
var settings_multthresh;
var settings_warntime;
var settings_logging;
var logging_checkbox;
var settings_ftp;
var settings_restore;
//collect all settings in setting_elems
var setting_elems = [];



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
        null, [obj], 'SINGLE'
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
                var obj_params = new tizen.ApplicationControlData('params', ftpParamsToString());
                var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
                    null,
                    null,
                    null, [obj, obj_params], 'SINGLE'
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

function delete_logs() {
	//make sure screen stays on!
	tizen.power.request('SCREEN', 'SCREEN_NORMAL');
	
	//delete log files!
    var obj = new tizen.ApplicationControlData('service_action', ['delete_logs']);
    var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj], 'SINGLE'
    );
    var appControlReplyCallback = {
    		// callee sent a reply
            onsuccess: function(data) {
                console.log('delete_logs: all log files deleted.');
                //end loading symbol...
                tau.closePopup();
                                
                //make screen turn off again:
            	tizen.power.release('SCREEN');
            },
            // callee returned failure
            onfailure: function() {
                console.log('delete_logs: deletion of logs failed!');
                //end loading popup
                tau.closePopup();
                //show upload failed popup
                tau.openPopup('#DeletionFailedPopup');
                
                //make screen turn off again:
            	tizen.power.release('SCREEN');
            }
        };
    try {
        tizen.application.launchAppControl(obj1,
            SERVICE_APP_ID,
            function() {
                console.log('delete_logs: Starting deletion of logs succeeded.');
            },
            function(e) {
                console.log('delete_logs: Starting deletion of logs failed : ' + e.message);
            }, appControlReplyCallback);
    } catch (e) {
        window.alert('delete_logs: Error when starting appcontrol for deleting logs! error msg:' + e.toString());
    }
}


//start service app and seizure detection (also makes sure that the corr checkbox is in the correct position!)
function start_service_app() {
	//disable settings!
    disable_settings();
	

    console.log('starting service app...');
    var obj = new tizen.ApplicationControlData('service_action', ['start']);
    var obj_params = new tizen.ApplicationControlData('params', analysisParamsToString());
    var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj, obj_params], 'SINGLE'
    );
    try {
        tizen.application.launchAppControl(obj1,
            SERVICE_APP_ID,
            function() {
                console.log('Launch Service succeeded');
                
                update_seizure_detection_checkbox();
            },
            function(e) {
                console.log('Launch Service failed : ' + e.message);
                tau.openPopup('#StartFailedPopup');
                
                update_seizure_detection_checkbox();
            }, null);
    } catch (e) {
        window.alert('Error when starting appcontrol for starting seizure detection! error msg:' + e.toString());
        
        update_seizure_detection_checkbox();
    }
}

//stop service app and seizure detection (also makes sure that the corr checkbox is in the correct position!)
function stop_service_app() {
	
    console.log('stopping service app...');
    var obj = new tizen.ApplicationControlData('service_action', ['stop']); //you'll find the app id in config.xml file.
    var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj], 'SINGLE'
    );
    try {
        tizen.application.launchAppControl(obj1,
            SERVICE_APP_ID,
            function() {
                console.log('Stopping Service Request succeeded');
                
                update_seizure_detection_checkbox();
            },
            function(e) {
                console.log('Stopping Service Request failed : ' + e.message);
                
                update_seizure_detection_checkbox();
            }, null);
    } catch (e) {
        window.alert('Error when starting appcontrol for stopping seizure detection! error msg:' + e.toString());
        
        update_seizure_detection_checkbox();
    }
}

function update_checkboxes() {
    //update logging and alarm checkbox
    console.log('updating logging checkbox');
    logging_checkbox.checked = params.logging;
    console.log('updating automatic_analysis checkbox');
    automatic_analysis_checkbox.checked = params.alarm;
}

//update seizure detection checkbox + make sure that settings are enabled or disabled accordingly!
function update_seizure_detection_checkbox() {
	disable_settings();
	
    //update checkbox indicating whether service app is running:
    console.log('ask service app if the sensor listener is running...');
    var obj = new tizen.ApplicationControlData('service_action', ['running?']);
    var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj], 'SINGLE'
    );
    var appControlReplyCallback = {
        // callee sent a reply
        onsuccess: function(data) {
            //update checkbox!
            start_stop_checkbox.checked = (data[0].value[0] === '1');
            console.log('received: ' + data[0].value[0]);
            console.log('updated checkbox! (to ' + (data[0].value[0] === '1') + ')');
            
            //enable settings if analysis is NOT running
            if(!start_stop_checkbox.checked) {
            	enable_settings();
            }
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
}


function recChildDisabled(elem, disabled) {
	if(disabled) elem.classList.add("disabled");
	else elem.classList.remove("disabled");
	elem.disabled = disabled;
	if(elem.children) {
		return;
	}
	if(elem.children.length == 0){
		return;
	} else {
		elem.children.forEach((el) => {
			recChildDisabled(el, disabled);
		});
	}
}

function enable_settings() {
	setting_elems.forEach( (elem) => {
		recChildDisabled(elem, false);
	});
}

function disable_settings() {
	setting_elems.forEach( (elem) => {
		recChildDisabled(elem, true);
	});
}


function toggle_analysis() {
    if (start_stop_checkbox.checked) {
        start_service_app();
    } else {
        stop_service_app();
    }
}


function toggle_logging() {
	params.logging = logging_checkbox.checked;
    save_params();
}


function add_alarms() {
	console.log('add_alarm: start.');
	remove_alarms();
	console.log('add_alarm: alarms removed.');

	//generate appcontrol to be called on remove_alarm
    var obj_stop = new tizen.ApplicationControlData('service_action', ['stop']); //you'll find the app id in config.xml file.
    var appcontrol_stop = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj_stop], 'SINGLE'
    );
   
    var obj_start = new tizen.ApplicationControlData('service_action', ['start']);
    var appcontrol_start = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj_start], 'SINGLE'
    );
	console.log('add_alarm: created application controls.');
	
	//var notification_content_stop = {content: 'Automatically started seizure detection!', actions: {vibration: true}};
	//var notification_stop = new tizen.UserNotification('SIMPLE', 'Epilarm', notification_content_stop);

	//var notification_content_start ={content: 'Automatically stopped seizure detection!', actions: {vibration: true}};
	//var notification_start = new tizen.UserNotification('SIMPLE', 'Epilarm', notification_content_start);
	//console.log('add_alarm: created notifications.');
	
	//adapt alarm date days, s.t. they are in the future (but at the next occurance of the specified time!)
	//change year, month, date to today's
	const today = new Date()
	params.alarm_start_date.setFullYear(today.getFullYear());
	params.alarm_start_date.setMonth(today.getMonth());
	params.alarm_start_date.setDate(today.getDate());

	params.alarm_stop_date.setFullYear(today.getFullYear());
	params.alarm_stop_date.setMonth(today.getMonth());
	params.alarm_stop_date.setDate(today.getDate());
	
	//in case these dates have already passed, change year, month, date to tomorrow's
	const tomorrow = new Date(today)	
	tomorrow.setDate(tomorrow.getDate() + 1)

	if (params.alarm_start_date < today) {
		params.alarm_start_date.setFullYear(tomorrow.getFullYear());
		params.alarm_start_date.setMonth(tomorrow.getMonth());
		params.alarm_start_date.setDate(tomorrow.getDate());
	}
	if (params.alarm_stop_date < today) {
		params.alarm_stop_date.setFullYear(tomorrow.getFullYear());
		params.alarm_stop_date.setMonth(tomorrow.getMonth());
		params.alarm_stop_date.setDate(tomorrow.getDate());
	}
	//params's alarm dates are now set to the next occuring time in the future!

	//var alarm_notification_start = new tizen.AlarmAbsolute(params.alarm_start_date, params.alarm_days); //TODO do we additionally need notifications?
	//var alarm_notification_stop = new tizen.AlarmAbsolute(params.alarm_stop_date, params.alarm_days);
	var alarm_start = new tizen.AlarmAbsolute(params.alarm_start_date);//, params.alarm_days);
	var alarm_stop = new tizen.AlarmAbsolute(params.alarm_stop_date);//, params.alarm_days);
	console.log('add_alarm: created alarms.');
	
	//tizen.alarm.addAlarmNotification(alarm_notification_start, notification_start);
	//tizen.alarm.addAlarmNotification(alarm_notification_stop, notification_stop);
	//console.log('add_alarm: scheduled notifications.');

	tizen.alarm.add(alarm_stop, APP_ID, appcontrol_stop);
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


function toggle_alarm() {
    if (automatic_analysis_checkbox.checked) {
        params.alarm = true;
        add_alarms();
    } else {
        params.alarm = false;
        remove_alarms();
    }
    save_params();
}


function analysisParamsToString() {
    return [params.minFreq.toString(), params.maxFreq.toString(), params.avgRoiThresh.toString(), params.multThresh.toString(), params.warnTime.toString(), (params.logging ? '1' : '0')];
}

function ftpParamsToString() {
    return [params.ftpHostname, params.ftpPort, params.ftpUsername, params.ftpPassword, params.ftpPath];
}

function reset_params() {
	//default values
    params = {
        minFreq: 3, //minimal freq of seizure-like movements (3Hz -> TODO verify using videos!)
        maxFreq: 8, //maximum freq of seizure-like movements (8Hz -> TODO verify using videos!)
        avgRoiThresh: 1.7, //average value above which the relevant (combined) freqs have to be for warning mode
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
        alarm_start_date: new Date(Date.parse("2020-11-23T06:00")),
        alarm_stop_date: new Date(Date.parse("2020-11-23T18:00")),
        alarm_days: ["MO", "TU", "WE", "TH", "FR", "SA", "SU"],
    };
}

//re-launch UI and start seizure detection
function test_start() {
    var obj_start = new tizen.ApplicationControlData('service_action', ['start']);
    var appcontrol_start = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj_start],
        'SINGLE'
    );
    tizen.application.launchAppControl(appcontrol_start,
            APP_ID,
            function() {
                console.log('Starting appcontrol succeeded.');
            },
            function(e) {
                console.log('Starting appcontrol failed : ' + e.message);
            }, null);

}

//re-launch UI and stop seizure detection
function test_stop() {
    var obj_start = new tizen.ApplicationControlData('service_action', ['stop']);
    var appcontrol_start = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
        null,
        null,
        null, [obj_start],
        'SINGLE'
    );
    tizen.application.launchAppControl(appcontrol_start,
            APP_ID,
            function() {
                console.log('Starting appcontrol succeeded.');
            },
            function(e) {
                console.log('Starting appcontrol failed : ' + e.message);
            }, null);

}


function addEventListeners() {
	var getURLcb = function(url) {
		return function(event) {
			if(!event.target.classList.contains("disabled")){
				console.log(event);
				tau.changePage(url);
			}
		}
	};
	
	settings_alarm_start.addEventListener("click", getURLcb("/contents/settings/automatic_start_picker.html"));

	settings_alarm_stop.addEventListener("click", getURLcb("contents/settings/automatic_stop_picker.html"));

	settings_minfreq.addEventListener("click", getURLcb("contents/settings/minfreq-picker.html"));
	settings_maxfreq.addEventListener("click", getURLcb("contents/settings/maxfreq-picker.html"));

	settings_avgthresh.addEventListener("click", getURLcb("contents/settings/avgroithresh-picker.html"));
	settings_multthresh.addEventListener("click", getURLcb("contents/settings/multthresh-picker.html"));
	
	settings_warntime.addEventListener("click", getURLcb("contents/settings/warntime-picker.html"));

	settings_restore.addEventListener("click", getURLcb("#ResetPopup"));
	
	
	//checkboxes
    start_stop_checkbox.addEventListener('change', toggle_analysis);
	
    automatic_analysis_checkbox.addEventListener('change', toggle_alarm);

    logging_checkbox.addEventListener('change', toggle_logging);
}

function loadListElements() {
	//define vars for all relevant list items and add eventlisteners!
    list = tau.widget.Listview(document.getElementById('main_list'))._items;
    
    //settings:    
    start_stop = list.find(el => el.id == 'start_stop');
    start_stop_checkbox = start_stop.childNodes[3].childNodes[1];
        
    settings_alarm_start = list.find(el => el.id == 'settings_alarm_start');
    settings_alarm_stop = list.find(el => el.id == 'settings_alarm_stop');
    
    automatic_analysis = list.find(el => el.id == 'automatic_analysis');
    automatic_analysis_checkbox = automatic_analysis.childNodes[3].childNodes[1];
    
    settings_logging = list.find(el => el.id == 'settings_logging');
    logging_checkbox = settings_logging.childNodes[3].childNodes[1];
    
    settings_minfreq = list.find(el => el.id == 'settings_minfreq');
    settings_maxfreq = list.find(el => el.id == 'settings_maxfreq');
    settings_avgthresh = list.find(el => el.id == 'settings_avgthresh');
    settings_multthresh = list.find(el => el.id == 'settings_multthresh');
    settings_warntime = list.find(el => el.id == 'settings_warntime');
    
    settings_ftp = list.find(el => el.id == 'settings_ftp');
    logs_upload = list.find(el => el.id == 'logs_upload');
    logs_delete = list.find(el => el.id == 'logs_delete');

    settings_restore = list.find(el => el.id == 'settings_restore');

    //collect all elements which should only be 'changeable' in case analysis is not running
    setting_elems = [settings_alarm_start, settings_alarm_stop, settings_logging, logging_checkbox,
                     settings_minfreq, settings_maxfreq, settings_avgthresh, settings_multthresh,
                     settings_warntime, settings_restore, logs_upload, logs_delete];
}


window.onload = function() {
    //leave screen on
    //tizen.power.request('SCREEN', 'SCREEN_NORMAL');
    
	//initialize all list elems
	loadListElements();

	//load user settings (or default!)
    load_params();
	
    //update 'simple' checkboxes (where service app must not be asked for)
    update_checkboxes();
    
    //seizure detection checkbox is updated when handling appcontrol:
    
    //check whether app was launched due to some appControl:
    var reqAppControl = tizen.application.getCurrentApplication().getRequestedAppControl();
    if (reqAppControl) {
    	
    	if (reqAppControl.appControl.operation != 'http://tizen.org/appcontrol/operation/default') {
    		//launched by non-default appcontrol, e.g., not started from drawer.
    		console.log('launched by appcontrol!');
    	    console.log(reqAppControl);
    	}
    	
        if (reqAppControl.appControl.data.length>0 && reqAppControl.appControl.operation === 'http://tizen.org/appcontrol/operation/service') {
        	//called from an appcontrol with operation name 'service'
        	if (reqAppControl.appControl.data[0].key === 'service_action' && reqAppControl.appControl.data[0].value[0] === 'start') {
        		//appcontrol request to start service app, also open Popup informing user!
        		console.log('appcontrol with service action _start_ received!');
        		
        		start_service_app(); //updates seizure detection checkbox aufter launch!
        		//open popup (timeout needed as otherwise the popup is not correctly aligned on the watch's display!)
        		setTimeout(function(event){    				
        			tau.openPopup('#AutomaticDetectionStartPopup');
    			},100)
    		} else if (reqAppControl.appControl.data[0].key === 'service_action' && reqAppControl.appControl.data[0].value[0] === 'stop') {
        		//appcontrol request to stop service app, also open Popup informing user!
        		console.log('appcontrol with service action _stop_ received!');
        		
    			stop_service_app(); //updates seizure detection checkbox aufter termination!
        		//open popup (timeout needed as otherwise the popup is not correctly aligned on the watch's display!)
    			setTimeout(function(event){    				
    				tau.openPopup('#AutomaticDetectionStopPopup');
    			},100)
			}
        }
    } else {
        console.log('not launched by appcontrol.');
        
        update_seizure_detection_checkbox();
    }
    
    //finally load elements and addEventlisteners, this makes sure that the checkbox is (probably) already updated before the user can change it!
    setTimeout(function(event){
    	addEventListeners();
    },200);

    console.log('UI started!');
};


window.onpageshow = function() {
	update_checkboxes();
	update_seizure_detection_checkbox();
};