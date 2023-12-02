package com.kuma.kmapi

import java.nio.ByteBuffer

interface TcpSocket {
    fun setSslFlags(flags: Int)
    fun setSslServerName(serverName: String)
    fun bind(host: String, port: Int): Boolean
    fun connect(host: String, port: Int, timeout: Int = 0): Boolean
    fun startSslHandshake(role: SslRole): Boolean
    fun send(str: String): Int
    fun send(arr: ByteArray): Int
    fun send(buf: ByteBuffer): Int
    fun close()
    fun setListener(listener: Listener.() -> Unit)
    companion object {
        fun create() = com.kuma.impl.TcpSocket()
    }

    interface Listener {
        fun onConnect(listener: (err: Int) -> Unit)
        fun onSslHandshake(listener: (err: Int) -> Unit)
        fun onSend(listener: (err: Int) -> Unit)
        fun onData(listener: (arr: ByteArray) -> Unit)
        fun onError(listener: (err: Int) -> Unit)
    }

    enum class SslRole(val value: Int) {
        CLIENT(0),
        SERVER(1)
    }
}