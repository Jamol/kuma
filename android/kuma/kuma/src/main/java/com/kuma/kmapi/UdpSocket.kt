package com.kuma.kmapi

import java.nio.ByteBuffer

interface UdpSocket {
    fun bind(host: String, port: Int, flags: Int = 0): Boolean
    fun connect(host: String, port: Int): Boolean
    fun send(str: String, host: String, port: Int): Int
    fun send(arr: ByteArray, host: String, port: Int): Int
    fun send(buf: ByteBuffer, host: String, port: Int): Int
    fun close()
    fun mcastJoin(addr: String, port: Int): Boolean
    fun mcastLeave(addr: String, port: Int): Boolean
    fun setListener(listener: Listener.() -> Unit)
    companion object {
        fun create() = com.kuma.impl.UdpSocket()
    }

    interface Listener {
        fun onData(listener: (arr: ByteArray, host: String, port: Int) -> Unit)
        fun onError(listener: (err: Int) -> Unit)
    }
}