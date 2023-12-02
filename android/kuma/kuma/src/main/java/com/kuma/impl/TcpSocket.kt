package com.kuma.impl

import com.kuma.kmapi.TcpSocket
import java.nio.ByteBuffer

class TcpSocket : com.kuma.kmapi.TcpSocket{
    private var handle: Long = 0
    private var listener: com.kuma.impl.TcpSocket.ListenerKt? = null
    override fun setSslFlags(flags: Int) {
        initNativeHandle()
        nativeSetSslFlags(handle, flags)
    }

    override fun setSslServerName(serverName: String) {
        initNativeHandle()
        nativeSetSslServerName(handle, serverName)
    }

    override fun bind(host: String, port: Int): Boolean {
        initNativeHandle()
        return nativeBind(handle, host, port)
    }

    override fun connect(host: String, port: Int, timeout: Int): Boolean {
        initNativeHandle()
        return nativeConnect(handle, host, port, timeout)
    }

    override fun startSslHandshake(role: TcpSocket.SslRole): Boolean {
        return nativeStartSslHandshake(handle, role.value)
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
    }

    override fun setListener(listener: TcpSocket.Listener.() -> Unit) {
        this.listener = ListenerKt().apply(listener)
    }

    private fun initNativeHandle() {
        if (handle.compareTo(0) == 0) {
            handle = nativeCreate()
        }
    }

    fun onConnect(err: Int) {
        listener?.onConnect(err)
    }

    fun onSslHandshake(err: Int) {
        listener?.onSslHandshake(err)
    }

    fun onSend(err: Int) {
        listener?.onSend(err)
    }

    fun onData(arr: ByteArray) {
        listener?.onData(arr)
    }

    fun onError(err: Int) {
        listener?.onError(err)
    }

    inner class ListenerKt : TcpSocket.Listener {
        private var connectListener: ((Int) -> Unit)? = null
        private var sslListener: ((Int) -> Unit)? = null
        private var sendListener: ((Int) -> Unit)? = null
        private var dataListener: ((arr: ByteArray) -> Unit)? = null
        private var errorListener: ((Int) -> Unit)? = null
        fun onConnect(err: Int) {
            connectListener?.invoke(err)
        }
        fun onSslHandshake(err: Int) {
            sslListener?.invoke(err)
        }
        fun onSend(err: Int) {
            sendListener?.invoke(err)
        }
        fun onData(arr: ByteArray) {
            dataListener?.invoke(arr)
        }
        fun onError(err: Int) {
            errorListener?.invoke(err)
        }

        override fun onConnect(listener: (err: Int) -> Unit) {
            connectListener = listener
        }

        override fun onSslHandshake(listener: (err: Int) -> Unit) {
            sslListener = listener
        }

        override fun onSend(listener: (err: Int) -> Unit) {
            sendListener = listener
        }

        override fun onData(listener: (arr: ByteArray) -> Unit) {
            dataListener = listener
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

    private external fun nativeCreate(): Long
    private external fun nativeSetSslFlags(handle: Long, flags: Int)
    private external fun nativeSetSslServerName(handle: Long, serverName: String)
    private external fun nativeBind(handle: Long, host: String, port: Int): Boolean
    private external fun nativeConnect(handle: Long, host: String, port: Int, timeout: Int): Boolean
    private external fun nativeStartSslHandshake(handle: Long, role: Int): Boolean
    private external fun nativeSendString(handle: Long, str: String): Int
    private external fun nativeSendArray(handle: Long, arr: ByteArray): Int
    private external fun nativeSendBuffer(handle: Long, buf: ByteBuffer): Int
    private external fun nativeClose(handle: Long)
    private external fun nativeDestroy(handle: Long)
    private external fun nativeCloseAndDestroy(handle: Long)
}