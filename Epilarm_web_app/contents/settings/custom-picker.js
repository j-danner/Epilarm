(function (window) {
	"use strict";
	var page, content, element, SAVED_NUMBER_KEY, spin = null;
	//choose correct vlaues based on calling page
	if (document.getElementById("minfreq-spin-page")) {
		page=document.getElementById("minfreq-spin-page");
		SAVED_NUMBER_KEY = "minFreq";
	} else if (document.getElementById("maxfreq-spin-page")) {
		page=document.getElementById("maxfreq-spin-page");
		SAVED_NUMBER_KEY = "maxFreq";
	} else if (document.getElementById("avgroithresh-spin-page")) {
		page=document.getElementById("avgroithresh-spin-page");
		SAVED_NUMBER_KEY = "avgRoiThresh";
	} else if (document.getElementById("multthresh-spin-page")) {
		page=document.getElementById("multthresh-spin-page");
		SAVED_NUMBER_KEY = "multThresh";
	} else if (document.getElementById("warntime-spin-page")) {
		page=document.getElementById("warntime-spin-page");
		SAVED_NUMBER_KEY = "warnTime";
	}
	
	content = page.querySelector(".ui-content");
	element = page.querySelector(".ui-spin");
	spin = window.tau.widget.Spin(element);
	
	//set start values to current params
	if(SAVED_NUMBER_KEY === "minFreq")  {
		spin.value((params[SAVED_NUMBER_KEY]-1.5)/0.5);
	} else if(SAVED_NUMBER_KEY === "maxFreq") {
		spin.value((params[SAVED_NUMBER_KEY]-6.0)/0.5);
	} else if(SAVED_NUMBER_KEY === "avgRoiThresh") {
		spin.value((params[SAVED_NUMBER_KEY]-1.5)/0.1);
	} else if(SAVED_NUMBER_KEY === "multThresh") {
		spin.value((params[SAVED_NUMBER_KEY]-1.5)/0.1);
	} else if(SAVED_NUMBER_KEY === "warnTime") {
		spin.value(params[SAVED_NUMBER_KEY]-5);
	}
	
	
	function onClickChange() { //if button is clicked
		//save selected value
		if(SAVED_NUMBER_KEY === "minFreq") params[SAVED_NUMBER_KEY] = spin.value()*0.5 + 1.5;
		else if(SAVED_NUMBER_KEY === "maxFreq") params[SAVED_NUMBER_KEY] = spin.value()*0.5 + 6.0;
		else if(SAVED_NUMBER_KEY === "avgRoiThresh") params[SAVED_NUMBER_KEY] = spin.value()*0.1 + 1.5;
		else if(SAVED_NUMBER_KEY === "multThresh") params[SAVED_NUMBER_KEY] = spin.value()*0.1 + 1.5;
		else if(SAVED_NUMBER_KEY === "warnTime") params[SAVED_NUMBER_KEY] = spin.value() + 5;

		save_params();
		window.history.back();
	}

	function onRotary(ev) {
		var step = 1;

		// get spin widget instance
		spin = window.tau.widget.Spin(element);

		if (spin.option("enabled")) {
			if (ev.detail.direction === "CW") {
				spin.value(spin.value() + step);
			} else {
				spin.value(spin.value() - step);
			}
		}
	}

	function onClick(ev) {
		var tau = window.tau;

		// get spin widget instance
		spin = window.tau.widget.Spin(element);

		if (tau.util.selectors.getClosestBySelector(ev.target, ".ui-spin") === null) {
			// click on background
			spin.option("enabled", false);
		} else {
			// Disable spin on click on selected item
			if (!spin.option("enabled")) {
				spin.option("enabled", true);
			} else if (ev.target.classList.contains("ui-spin-item-selected")) {
				spin.option("enabled", false);
			}
		}
	}

	/**
	 * Template initializing
	 */
	function init() {
		page.addEventListener("pageshow", function () {
			var tau = window.tau;

			// create spin widget
			spin = tau.widget.Spin(element);

			// enable spin on click
			content.addEventListener("vclick", onClick);

			element.addEventListener("spinchange", function () {
				/*
				Event "spinchange" is not triggering when value is changing by .value() method.
				eg. "rotarydetent" does not trigger "spinchange" because of value
				    has changed by .value()
				*/
			});

			element.addEventListener("spinstep", function () {
				//console.log("spinstep");
			});

			// Spin widget doesn't have inner support for rotary event
			// add rotary event
			document.addEventListener("rotarydetent", onRotary);

			// cleanup widget in order to avoid memory leak
			tau.event.one(page, "pagehide", function () {
				document.removeEventListener("rotarydetent", onRotary);
				document.querySelector(".ui-footer .ui-btn").removeEventListener("click", onClickChange);
				content.removeEventListener("vclick", onClick);
				spin.destroy();
			});
		});

		document.querySelector(".ui-footer .ui-btn").addEventListener("click", onClickChange);
	}

	// init application
	init();

})(window);
