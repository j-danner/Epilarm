
//variables that can be changed in settings:
var saveData = true; //boolean to decide if sensordata should be stored

//parameters TODO should be transferred to service app at startup!
var minFreq = 3; //minimal freq of seizure-like movements (3Hz -> TODO verify using videos!)
var maxFreq = 8; //maximum freq of seizure-like movements (8Hz -> TODO verify using videos!)
var avgRoiThresh = 2.3; //average value above which the relevant (combined) freqs have to be for warning mode
var multThresh = 2.5; //threshhold for ration of average of non-relevant freqs and relevant freqs required for warning mode (as sum of the rations of the three dimensions)
var warnTime = 10;//time after which a continuous WARNING state raises an ALARM

//ringbuffers for data received from service app
var freq = new createRingBuffer(15*2); freq.fill(0);

var SERVICE_APP_ID = 'QOeM6aBGp0.epilarm_sensor_service';

//returns params [minFreq, maxFreq, avgRoiThresh, multThresh, warnTime] each as string
//TODO read those values from local strorage
function load_params_as_str() {
	return [minFreq.toString(), maxFreq.toString(), avgRoiThresh.toString(), multThresh.toString(), warnTime.toString()];
}

function start_service_app()  {
	console.log('starting service app...');
	var obj = new tizen.ApplicationControlData('service_action', ['start']);
	var params = new tizen.ApplicationControlData('params', load_params_as_str());
	var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
			null,
			null,
			null,
			[obj, params] 
	);
	tizen.application.launchAppControl(obj1,
			SERVICE_APP_ID,
			function() {console.log('Launch Service succeeded'); },
			function(e) {console.log('Launch Service failed : ' + e.message);}, null);
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
				document.getElementById("start_stop").checked = (data[0].value[0] == 1);
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
	tizen.power.request('SCREEN', 'SCREEN_NORMAL');
	
	console.log('UI started!');
	update_start_stop_checkbox(); // make sure that checkbox is in correct state on startup
	
};


