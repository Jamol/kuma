package com.kuma.impl

import com.kuma.kmapi.HttpRequest
import java.nio.ByteBuffer

class HttpRequest(val version: String) : com.kuma.kmapi.HttpRequest {
    private var handle: Long = 0
    private var listener: ListenerKt? = null
    override fun setSslFlags(flags: Int) {
        if (handle.compareTo(0) == 0) {
            handle = nativeCreate(version)
        }
        nativeSetSslFlags(handle, flags)
    }

    override fun addHeader(key: String, value: String) {
        if (handle.compareTo(0) == 0) {
            handle = nativeCreate(version)
        }
        nativeAddHeader(handle, key, value)
    }

    override fun sendRequest(method: String, url: String): Int {
        if (handle.compareTo(0) == 0) {
            handle = nativeCreate(version)
        }
        return nativeSendRequest(handle, method, url)
    }

    override fun sendData(str: String): Int {
        return nativeSendString(handle, str)
    }

    override fun sendData(arr: ByteArray): Int {
        return nativeSendArray(handle, arr)
    }

    override fun sendData(buf: ByteBuffer): Int {
        return nativeSendBuffer(handle, buf)
    }

    override fun reset() {
        nativeReset(handle)
    }

    override fun close() {
        nativeCloseAndDestroy(handle)
        handle = 0
    }

    override fun setListener(listener: HttpRequest.Listener.() -> Unit) {
        this.listener = ListenerKt().apply(listener)
    }


    fun onHeaderComplete() {
        listener?.onHeaderComplete()
    }
    fun onSend(err: Int) {
        listener?.onSend(err)
    }
    fun onData(arr: ByteArray) {
        listener?.onData(arr)
    }
    fun onResponseComplete() {
        listener?.onResponseComplete()
    }
    fun onError(err: Int) {
        listener?.onError(err)
    }


    inner class ListenerKt : com.kuma.kmapi.HttpRequest.Listener {
        private var headerListener: (() -> Unit)? = null
        private var sendListener: ((Int) -> Unit)? = null
        private var dataListener: ((arr: ByteArray) -> Unit)? = null
        private var rspListener: (() -> Unit)? = null
        private var errorListener: ((Int) -> Unit)? = null
        fun onHeaderComplete() {
            headerListener?.invoke()
        }
        fun onSend(err: Int) {
            sendListener?.invoke(err)
        }
        fun onData(arr: ByteArray) {
            dataListener?.invoke(arr)
        }
        fun onResponseComplete() {
            rspListener?.invoke()
        }
        fun onError(err: Int) {
            errorListener?.invoke(err)
        }

        override fun onHeaderComplete(listener: () -> Unit) {
            headerListener = listener
        }

        override fun onSend(listener: (err: Int) -> Unit) {
            sendListener = listener
        }

        override fun onData(listener: (arr: ByteArray) -> Unit) {
            dataListener = listener
        }

        override fun onResponseComplete(listener: () -> Unit) {
            rspListener = listener
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
    private external fun nativeAddHeader(handle: Long, key: String, value: String)
    private external fun nativeSendRequest(handle: Long, method: String, url: String): Int
    private external fun nativeSendString(handle: Long, str: String): Int
    private external fun nativeSendArray(handle: Long, arr: ByteArray): Int
    private external fun nativeSendBuffer(handle: Long, buf: ByteBuffer): Int
    private external fun nativeReset(handle: Long)
    private external fun nativeClose(handle: Long)
    private external fun nativeDestroy(handle: Long)
    private external fun nativeCloseAndDestroy(handle: Long)
}