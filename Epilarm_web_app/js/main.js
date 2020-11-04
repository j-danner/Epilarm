

//parameters TODO should be transferred to service app at startup!
var params = {
	minFreq : 3, //minimal freq of seizure-like movements (3Hz -> TODO verify using videos!)
	maxFreq : 8, //maximum freq of seizure-like movements (8Hz -> TODO verify using videos!)
	avgRoiThresh : 2.3, //average value above which the relevant (combined) freqs have to be for warning mode
	multThresh : 2.5, //threshhold for ration of average of non-relevant freqs and relevant freqs required for warning mode (as sum of the rations of the three dimensions)
	warnTime : 10, //time after which a continuous WARNING state raises an ALARM
	

	//variables that can be changed in settings:
	logging : true, //boolean to decide if sensordata should be stored
	ftpHostname : '192.168.178.33', //address of your (local) ftp server to which logs are uploaded
	ftpPort : '21', //port under which the broker is found
	ftpUsername : 'sam-gal-act-2', //username
	ftpPassword : 'T5tUZKVKWq8FPAh5', //password
	ftpPath : 'share/epilarm/log', //path on ftp server to store files in
		
	toString : function(){
		return [this.minFreq.toString(), this.maxFreq.toString(), this.avgRoiThresh.toString(), this.multThresh.toString(), this.warnTime.toString(), (this.logging ? "1" : "0"),
				this.ftpHostname, this.ftpPort, this.ftpUsername, this.ftpPassword, this.ftpPath];
	}
};
	
//ringbuffers for data received from service app
var freq = new createRingBuffer(15*2); freq.fill(0);

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
        	//default values are always used when loading fails!
    	} else {
    		params = JSON.parse(localStorage.getItem('params'));
    		console.log('params loaded!');
    	}
    } else {
    	console.log('load_params: no localStorage in window found, using default values!');
   }
}

function start_service_app()  {
	console.log('starting service app...');
	var obj = new tizen.ApplicationControlData('service_action', ['start']);
	var obj_params = new tizen.ApplicationControlData('params', params.toString());
	var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
			null,
			null,
			null,
			[obj, obj_params] 
	);
	tizen.application.launchAppControl(obj1,
			SERVICE_APP_ID,
			function() {console.log('Launch Service succeeded'); },
			function(e) {console.log('Launch Service failed : ' + e.message);}, null);
}


function stop_service_app()  {
	console.log('stopping service app...');
	var obj = new tizen.ApplicationControlData('service_action', ['stop']); //you'll find the app id in config.xml file.
	var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
			null,
			null,
			null,
			[obj] 
	);
	tizen.application.launchAppControl(obj1,
			SERVICE_APP_ID,
			function() {console.log('Stopping Service succeeded'); },
			function(e) {console.log('Stopping Service failed : ' + e.message);}, null);
}

function update_start_stop_checkbox() {
	console.log('ask service app if the sensor listener is running...');
	var obj = new tizen.ApplicationControlData('service_action', ['running?']); //you'll find the app id in config.xml file.
	var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
			null,
			null,
			null,
			[obj] 
	);
	var appControlReplyCallback = {
			// callee sent a reply
			onsuccess: function(data) {
				//console.log('reply is: ' + data[0].value[0]);
				//save result:
				document.getElementById("start_stop").checked = (data[0].value[0] === 1);
				console.log('updated checkbox!');
			},
			// callee returned failure
			onfailure: function() {
				console.log('reply failed');
			}
	};
	
	tizen.application.launchAppControl(obj1,
			SERVICE_APP_ID,
			function() {console.log('Checking whether sensor listener is running succeeded'); },
			function(e) {console.log('Checking whether sensor listener is running failed : ' + e.message);}, 
			appControlReplyCallback);
}



function start_stop(id){
  if(document.getElementById(id).checked) {
	  start_service_app();
  }
  else{
	  stop_service_app();
  }
}


window.onload = function () {
    // TODO:: Do your initialization job

	//leave screen on
	//tizen.power.request('SCREEN', 'SCREEN_NORMAL');
	
	//make sure that checkbox is in correct state on startup
	update_start_stop_checkbox();
	//load user settings
	load_params();

	console.log('UI started!');
	
	start_service_app();
	//setTimeout(() => {  stop_service_app(); }, 25000);	//automatically stop sensor service after 12secs
};


