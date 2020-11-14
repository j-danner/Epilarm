//parameters, must be initialized by reset_params() or loaded from localstorage. Simply use load_params() once!
var params;

var SERVICE_APP_ID = 'QOeM6aBGp0.epilarm_sensor_service';

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

function start_ftp_upload() {
	console.log('ftp_upload: checking if wifi is on...');
	//TODO automatically switch on wifi if necessary...
	/*if(navigator.connection.type != Connection.WIFI) {
		console.log("ftp_upload: wifi is on, ftp upload can be started!");
	} else {
		//switch wifi on... 
		
	}*/
	//start data compression!
    console.log('stopping service app...');
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
                
            	//if compression of logs is done, start ftp-upload
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
            },
            // callee returned failure
            onfailure: function() {
                console.log('ftp_upload: compression failed');
                //end loading popup
                tau.closePopup();
                //show upload failed popup
                tau.openPopup('#CompressionFailedPopup'); //TODO
            }
        };
    try {
        tizen.application.launchAppControl(obj1,
            SERVICE_APP_ID,
            function() {
                console.log('Starting compression of logs succeeded.');
            },
            function(e) {
                console.log('Starting compression of logs failed : ' + e.message);
            }, null);
    } catch (e) {
        window.alert('Error when starting appcontrol for compressing logs! error msg:' + e.toString());
    }

}


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
            }, null);
    } catch (e) {
        window.alert('Error when starting appcontrol for starting seizure detection! error msg:' + e.toString());
        running=false;
    }
}

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
                console.log('Stopping Service succeeded');
            },
            function(e) {
                console.log('Stopping Service failed : ' + e.message);
            }, null);
    } catch (e) {
        window.alert('Error when starting appcontrol for stoppint seizure detection! error msg:' + e.toString());
    }
}

function update_start_stop_checkbox() {
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
        },
        // callee returned failure
        onfailure: function() {
            console.log('reply failed');
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

function start_stop(id) {
    if (document.getElementById(id).checked) {
        start_service_app();
    } else {
        stop_service_app();
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

    //make sure that checkboxes are in correct state on startup
    update_start_stop_checkbox();

    console.log('UI started!');

    //start_service_app();
    //setTimeout(() => {  stop_service_app(); }, 25000);	//automatically stop sensor service after 12secs
};