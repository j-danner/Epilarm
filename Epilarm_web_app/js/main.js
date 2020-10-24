
//variables that can be changed in settings:
var saveData = true; //boolean to decide if sensordata should be stored

//parameters TODO should be transferred to service app at startup!
var minFreq = 2.5; //minimal freq of seizure-like movements (3Hz -> TODO verify using videos!)
var maxFreq = 7.5; //maximum freq of seizure-like movements (8Hz -> TODO verify using videos!)
var avgRoiThresh = 6; //average value above which the relevant (combined) freqs have to be for warning mode
var warnMultThresh = 2.5; //threshhold for ration of average of non-relevant freqs and relevant freqs required for warning mode (as sum of the rations of the three dimensions)
var warnTime = 10;//time after which a continuous WARNING state raises an ALARM
var alarmState = 0;

//ringbuffers for data received from service app
var freq = new createRingBuffer(15*2); freq.fill(0);

var MYSERVICE_APP_ID = 'QOeM6aBGp0.epilarm_sensor_service';


function start_service_app()  {
	console.log('starting service app...');
	var obj = new tizen.ApplicationControlData('service_action', ['start']); //you'll find the app id in config.xml file.
	var obj1 = new tizen.ApplicationControl('http://tizen.org/appcontrol/operation/service',
			null,
			null,
			null,
			[obj] 
	);
	tizen.application.launchAppControl(obj1,
			MYSERVICE_APP_ID,
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
			MYSERVICE_APP_ID,
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
	
	console.log('UI running!');
};


