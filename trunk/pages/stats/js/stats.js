function updateTimestamp(timestamp) {
    var date = new Date();

    date.setTime(timestamp * 1000);

    setText('timestamp', date.toString())
}

function updateError(type, error) {
    error = dojo.errorToString(error);

    setText('error', error);
}

function setText(id, text) {
    var node = document.getElementById(id);

	if (node != null) {
		node.innerHTML = text;
	}
}

function updateStats() {
	if (UpdateStats == 1) {
		setTimeout('getData()', 1000);
	}
}

function controlStats(action) {
	pauseNode = document.getElementById('pause');
	resumeNode = document.getElementById('resume');
	
	if (action == "pause") {
		pauseNode.style.display = "none"
		resumeNode.style.display = "block"
		UpdateStats = 0
	} else {
		pauseNode.style.display = "block"
		resumeNode.style.display = "none"
		UpdateStats = 1
		updateStats()
	}
}