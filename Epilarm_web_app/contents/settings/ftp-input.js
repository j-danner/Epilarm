/*global tau */
(function () {
	var page = document.getElementById("ftp-input"),
		inputHostname = page.querySelector("input[name=\"hostname\"]"),
		inputPort = page.querySelector("input[name=\"port\"]"),
		inputUsername = page.querySelector("input[name=\"username\"]"),
		inputPassword = page.querySelector("input[name=\"password\"]"),
		inputPath = page.querySelector("input[name=\"path\"]"),
		hostnameWidget,
		portWidget,
		userWidget,
		pwWidget,
		pathWidget;
	
	var saveButton = page.querySelector(".ui-btn");

	function init() {
		hostnameWidget = tau.widget.TextInput(inputHostname);
		portWidget = tau.widget.TextInput(inputPort);
		userWidget = tau.widget.TextInput(inputUsername);
		pwWidget = tau.widget.TextInput(inputPassword);
		pathWidget = tau.widget.TextInput(inputPath);
		
		saveButton.addEventListener("vclick", saveData);

		inputHostname.value = params.ftpHostname;
		inputPort.value = params.ftpPort;
		inputUsername.value = params.ftpUsername;
		inputPassword.value = params.ftpPassword;
		inputPath.value = params.ftpPath;
	}

	function saveData() {
		if(inputHostname.value)	params.ftpHostname = inputHostname.value;
		if(inputPort.value) params.ftpPort = inputPort.value;
		if(inputUsername.value)	params.ftpUsername = inputUsername.value;
		if(inputPassword.value)	params.ftpPassword = inputPassword.value;
		if(inputPath.value)	params.ftpPath = inputPath.value;
		
		save_params();

		window.history.back();
	}

	function destroy() {
		hostnameWidget.destroy();
		portWidget.destroy();
		userWidget.destroy();
		pwWidget.destroy();
		pathWidget.destroy();
		
		saveButton.removeEventListener("vclick", saveData);
	}

	page.addEventListener("pagebeforeshow", init);
	//init();
	page.addEventListener("pagehide", destroy);
}());
