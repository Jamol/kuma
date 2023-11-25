package com.kuma.impl

import java.nio.ByteBuffer

class WebSocket : com.kuma.kmapi.WebSocket{
    private var handle: Long = 0
    override fun open(url: String): Boolean {
        if (handle.compareTo(0) != 0) {
            close()
        }
        handle = nativeCreate()
        return nativeOpen(handle, url)
    }

    override fun send(str: String): Int {
        return nativeSendString(handle, str)
    }

    override fun send(arr: ByteArray): Int {
        return nativeSendArray(handle, arr)
    }

    override fun send(buf: ByteBuffer): Int {
        return nativeSendBuffer(handle, buf)
    }

    override fun close() {
        nativeCloseAndDestroy(handle)
        handle = 0
    }

    fun onOpen(err: Int) {

    }

    fun onSend(err: Int) {

    }

    fun onData(data: ByteArray, fin: Boolean) {

    }

    fun onData(data: String, fin: Boolean) {

    }

    fun onClose(err: Int) {

    }

    private external fun nativeCreate(): Long
    private external fun nativeOpen(handle: Long, url: String): Boolean
    private external fun nativeSendString(handle: Long, str: String): Int
    private external fun nativeSendArray(handle: Long, arr: ByteArray): Int
    private external fun nativeSendBuffer(handle: Long, buf: ByteBuffer): Int
    private external fun nativeClose(handle: Long)
    private external fun nativeDestroy(handle: Long)
    private external fun nativeCloseAndDestroy(handle: Long)
}