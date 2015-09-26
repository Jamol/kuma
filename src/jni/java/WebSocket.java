package com.jamol.kuma;

import android.util.Log;

public class WebSocket {
    private WSListener listener;
    public WebSocket(WSListener l) {
        listener = l;
    }
    
    public int open(String ws_url) {
        return connect(ws_url);
    }
    
    public int send(String data) {
        return sendString(nativeHandle, data);
    }
    
    public int send(byte[] data) {
        return sendArray(nativeHandle, data);
    }
    
    public int close() {
        return close(nativeHandle);
    }
    
    public void onConnect(int err) {
        if(listener != null) {
            listener.onConnect(err);
        }
    }
    
    public void onData(String str) {
        if(listener != null) {
            listener.onData(str);
        }
    }
    
    public void onData(byte[] data) {
        if(listener != null) {
            listener.onData(data);
        }
    }
    
    public void onClose() {
        if(listener != null) {
            listener.onClose();
        }
    }
    
    private native int connect(String ws_url);
    private native int sendString(long handle, String str);
    private native int sendArray(long handle, byte[] data);
    private native int close(long handle);
    
    private long nativeHandle = 0;
    public interface WSListener {
        void onConnect(int err);
        void onData(String data);
        void onData(byte[] data);
        void onClose();
    }
}
