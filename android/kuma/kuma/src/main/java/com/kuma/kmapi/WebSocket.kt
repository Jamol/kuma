package com.kuma.kmapi

import java.nio.ByteBuffer

interface WebSocket {
    fun setSslFlags(flags: Int)
    fun setOrigin(origin: String)
    fun addHeader(key: String, value: String)
    fun open(url: String): Boolean
    fun send(str: String): Int
    fun send(arr: ByteArray): Int
    fun send(buf: ByteBuffer): Int
    fun close()
    fun setListener(listener: Listener.() -> Unit)
    companion object {
        fun create(ver: String) = com.kuma.impl.WebSocket(ver)
    }

    interface Listener {
        fun onOpen(listener: (err: Int) -> Unit)
        fun onSend(listener: (err: Int) -> Unit)
        fun onString(listener: (str: String, fin: Boolean) -> Unit)
        fun onArray(listener: (arr: ByteArray, fin: Boolean) -> Unit)
        fun onClose(listener: (err: Int) -> Unit)
    }
}