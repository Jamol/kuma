
; (function (window, kuma, undefined) {
    var exports = kuma;

    function testHttp(url, progress) {
        var uid = kuma.allocUniqueId(),
            xhr = null,
            recvBytes = 0,
            i
	    ;

        console.info("testHttp, uid=" + uid + ", url=" + url);
        xhr = new AjaxURLRequest(uid);
        xhr.ondata = function (d) {
            recvBytes += d.length;
            console.info("testHttp.ondata, uid=" + uid + ", len=" + d.length + ", total=" + recvBytes);
            progress(recvBytes);
        }
        xhr.onsuccess = function (d) {
            if (d) {
                recvBytes += d.length;
            }
            console.info("testHttp.onsuccess, uid=" + uid + ", total=" + recvBytes);
            progress(recvBytes, true);
        }
        xhr.onerror = function(e) {
            console.info("testHttp.onerror, uid=" + uid + ", e=" + e);
        }
        xhr.open("GET", url);
    }

    function createXmlHttpRequest() {
		var xmlhttp = null;
        if (navigator.appVersion.indexOf("MSIE") != -1 && null !== window.XDomainRequest) {
        }
		if (window.XMLHttpRequest) {
			try { xmlhttp = new XMLHttpRequest(); } catch (e) { }
		} else if (window.ActiveXObject) {
			do {
				try {
					xmlhttp = new ActiveXObject("Msxml3.XMLHTTP");
					break;
				} catch (e) { }
				try {
					xmlhttp = new ActiveXObject("Msxml2.XMLHTTP");
					break;
				} catch (e) { }
				try {
					xmlhttp = new ActiveXObject("Msxml.XMLHTTP");
					break;
				} catch (e) { }
				try {
					xmlhttp = new ActiveXObject("Microsoft.XMLHTTP");
					break;
				} catch (e) { }
			} while (0);
		}
		return xmlhttp;
	}

    function AjaxURLRequest(id) {
		var uid = id,
		    xhr = null,
            self = this;

		function cleanup() {
			if (xhr) {
				xhr.abort();
			}
			xhr = null;
		}

		this.close = function() {
			cleanup();
		};
		this.open = function(method, url) {
            console.info("AjaxURLRequest.open, uid=" + uid + ", method=" + method + ", url=" + url);
			var offset = 0;

			function onError(e) {
                if (self.onerror) {
                    self.onerror(e);
                }
			}

			function onData() {
				if (!self.ondata || !xhr || !xhr.responseText) {
					return;
				}
				if (0 === offset) {
					self.ondata(xhr.responseText);
				} else if (xhr.responseText.length > offset) {
					var str = xhr.responseText.substring(offset);
					self.ondata(str);
				}
				offset = xhr.responseText.length;
			}

			function onSuccess() {
				if (self.ondata) {
					onData();
					if (self.onsuccess) {
						self.onsuccess("");
					}
				} else if (self.onsuccess) {
					self.onsuccess(xhr.responseText);
				}
			}

			if (navigator.appVersion.indexOf("MSIE") != -1 && null !== window.XDomainRequest) {
				xhr = new XDomainRequest();
				xhr.onerror = onError;
				xhr.ontimeout = onError;
				xhr.onprogress = onData;
				xhr.onload = onSuccess;
			    //xhr.timeout = 30000;
				try {
					xhr.open(method, url);
					xhr.send(null);
				}catch(e) {
					console.info("AjaxURLRequest.open, uid=" + uid + ", e=" + e);
				}
			} else {
				xhr = createXmlHttpRequest();
				xhr.onreadystatechange = function() {
					switch (xhr.readyState) {
					case 3:
						onData();
						break;
					case 4:
						if (xhr.status == 200) {
							onSuccess();
						}
						else {
							onError(xhr.status);
						}
						break;
					}
				};
				try {
					xhr.open(method, url, true);
                    xhr.send(null);
				} catch(e) {
					console.info("AjaxURLRequest.open, uid=" + uid + ", e=" + e);
				}
			}
			return 0;
		}
	}

    exports.testHttp = testHttp;
})(window, kuma);
