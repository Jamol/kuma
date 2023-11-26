package com.kuma.kmapi

import java.nio.ByteBuffer

interface HttpRequest {
    fun setSslFlags(flags: Int)
    fun addHeader(key: String, value: String)
    fun sendRequest(method: String, url: String): Int
    fun sendData(str: String): Int
    fun sendData(arr: ByteArray): Int
    fun sendData(buf: ByteBuffer): Int
    fun reset()
    fun close()
    fun setListener(listener: HttpRequest.Listener.() -> Unit)
    companion object {
        fun create(ver: String) = com.kuma.impl.HttpRequest(ver)
    }

    interface Listener {
        fun onHeaderComplete(listener: () -> Unit)
        fun onSend(listener: (err: Int) -> Unit)
        fun onData(listener: (arr: ByteArray) -> Unit)
        fun onResponseComplete(listener: () -> Unit)
        fun onError(listener: (err: Int) -> Unit)
    }
}