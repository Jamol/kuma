package com.kuma.impl

import com.kuma.kmapi.WebSocket
import java.nio.ByteBuffer

class WebSocket(val version: String) : com.kuma.kmapi.WebSocket {
    private var handle: Long = 0
    private var listener: ListenerKt? = null
    override fun setSslFlags(flags: Int) {
        nativeSetSslFlags(handle, flags)
    }

    override fun setOrigin(origin: String) {
        nativeSetOrigin(handle, origin)
    }

    override fun addHeader(key: String, value: String) {
        nativeAddHeader(handle, key, value)
    }

    override fun open(url: String): Boolean {
        if (handle.compareTo(0) != 0) {
            close()
        }
        handle = nativeCreate(version)
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

    override fun setListener(listener: WebSocket.Listener.() -> Unit) {
        this.listener = ListenerKt().apply(listener)
    }

    fun onOpen(err: Int) {
        listener?.onOpen(err)
    }

    fun onSend(err: Int) {
        listener?.onSend(err)
    }

    fun onData(arr: ByteArray, fin: Boolean) {
        listener?.onData(arr, fin)
    }

    fun onData(str: String, fin: Boolean) {
        listener?.onData(str, fin)
    }

    fun onError(err: Int) {
        listener?.onError(err)
    }


    inner class ListenerKt : WebSocket.Listener {
        private var openListener: ((Int) -> Unit)? = null
        private var sendListener: ((Int) -> Unit)? = null
        private var strDataListener: ((str: String, fin: Boolean) -> Unit)? = null
        private var arrDataListener: ((arr: ByteArray, fin: Boolean) -> Unit)? = null
        private var errorListener: ((Int) -> Unit)? = null
        fun onOpen(err: Int) {
            openListener?.invoke(err)
        }
        fun onSend(err: Int) {
            sendListener?.invoke(err)
        }
        fun onData(str: String, fin: Boolean) {
            strDataListener?.invoke(str, fin)
        }
        fun onData(arr: ByteArray, fin: Boolean) {
            arrDataListener?.invoke(arr, fin)
        }
        fun onError(err: Int) {
            errorListener?.invoke(err)
        }

        override fun onOpen(listener: (err: Int) -> Unit) {
            openListener = listener
        }

        override fun onSend(listener: (err: Int) -> Unit) {
            sendListener = listener
        }

        override fun onString(listener: (str: String, fin: Boolean) -> Unit) {
            strDataListener = listener
        }

        override fun onArray(listener: (arr: ByteArray, fin: Boolean) -> Unit) {
            arrDataListener = listener
        }

        override fun onError(listener: (err: Int) -> Unit) {
            errorListener = listener
        }
    }

    companion object {
        init {
            NativeLib.load()
        }
    }

    private external fun nativeCreate(ver: String): Long
    private external fun nativeSetSslFlags(handle: Long, flags: Int)
    private external fun nativeSetOrigin(handle: Long, origin: String)
    private external fun nativeAddHeader(handle: Long, key: String, value: String)
    private external fun nativeOpen(handle: Long, url: String): Boolean
    private external fun nativeSendString(handle: Long, str: String): Int
    private external fun nativeSendArray(handle: Long, arr: ByteArray): Int
    private external fun nativeSendBuffer(handle: Long, buf: ByteBuffer): Int
    private external fun nativeClose(handle: Long)
    private external fun nativeDestroy(handle: Long)
    private external fun nativeCloseAndDestroy(handle: Long)
}