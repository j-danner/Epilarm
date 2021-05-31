var running; //html element that tells if Epilarm is running

var SERVICE_APP_ID = tizen.application.getCurrentApplication().appInfo.packageId + '.epilarm_sensor_service'; //'QOeM6aBGp0.epilarm_sensor_service';

function update_running_info() {  
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
        	if(data[0].value[0] === '1') {
        		running.innerHTML = 'Epilarm analysis running!';
        	} else {
        		running.innerHTML = 'Epilarm deactivated.';
        	}
            console.log('received: ' + data[0].value[0]);
            console.log('updated running.innerHTML.');
        },
        // callee returned failure
        onfailure: function() {
            console.log('reply failed!');
            //TODO start web app and inform on fail?! (web-app here shows #RunningFailedPopup.)
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
};



window.onload = function() {
  console.log('+[window.onload]');
  // initialize vars
  running = document.getElementById('running');
  
  //register eventlistener for visibilitychange, i.e., if 
  document.addEventListener("visibilitychange", function() {
    if (document.visibilityState === 'visible') {
      //update isRunning info
      update_running_info();
    } else {
  	  //do nothing
    }
  });
};
