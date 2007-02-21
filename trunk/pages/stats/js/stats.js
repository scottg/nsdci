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
