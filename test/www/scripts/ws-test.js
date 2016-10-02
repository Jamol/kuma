
; (function (window, kuma, undefined) {
    var exports = kuma;
	function testWebSocket(url, options) {
	    var uid = kuma.allocUniqueId(),
            ws = null,
            testData = null,
            testLength = 1024,
            testCount = 1000,
            sendCount = 0,
            recvCount = 0,
            i
	    ;

        testData = new Uint8Array(testLength);
        for (i=0; i<testLength; ++i) {
            testData[i] = "e".charCodeAt(0);
        }

		if (!window.WebSocket) {
		    return false;
		}
		console.info("testWebSocket, uid=" + uid + ", url=" + url);
		ws = new WebSocket(url);
		ws.binaryType = "arraybuffer";

		ws.onmessage = function (evt) {
		    if (null == ws) { // closed
		        return;
		    }
		    var d = new Uint8Array(evt.data);
            ++recvCount;
            console.info("testWebSocket.onmessage, uid=" + uid + ", len=" + d.length + ", count=" + recvCount);
		    if (d === null || d.length === 0) {
		        return;
		    }
            if (recvCount >= testCount) {
                ws.close();
                ws = null;
                console.info("testWebSocket, test completed, uid=" + uid);
                return;
            }
            sendTestData();
		}

		ws.onerror = function (evt) {
		    if (null == ws) { // closed
		        return;
		    }
		    console.warn("testWebSocket.onerror, uid=" + uid + ", evt:", evt);
		}

		ws.onclose = function (evt) {
		    if (null == ws) { // closed
		        return;
		    }
		    console.warn("testWebSocket.onclose, uid=" + uid + ", evt:", evt);
		}

		ws.onopen = function (evt) {
		    console.info("testWebSocket.onopen, uid=" + uid);
            sendTestData();
		}

        function sendTestData() {
            if (null == ws) {
                return;
            }
            if (++sendCount > testCount) {
                return;
            }
            //console.info("testWebSocket.sendTestData, uid=" + uid + ", count=" + sendCount);
            ws.send(testData.buffer);
        }
		return true;
	}

    exports.testWebSocket = testWebSocket;
})(window, kuma);
