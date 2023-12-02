package com.kuma.impl

import com.kuma.kmapi.UdpSocket
import java.nio.ByteBuffer

class UdpSocket : com.kuma.kmapi.UdpSocket{
    private var handle: Long = 0
    private var listener: com.kuma.impl.UdpSocket.ListenerKt? = null

    override fun bind(host: String, port: Int, flags: Int): Boolean {
        initNativeHandle()
        return nativeBind(handle, host, port, flags)
    }

    override fun connect(host: String, port: Int): Boolean {
        initNativeHandle()
        return nativeConnect(handle, host, port)
    }

    override fun send(str: String, host: String, port: Int): Int {
        return nativeSendString(handle, str, host, port)
    }

    override fun send(arr: ByteArray, host: String, port: Int): Int {
        return nativeSendArray(handle, arr, host, port)
    }

    override fun send(buf: ByteBuffer, host: String, port: Int): Int {
        return nativeSendBuffer(handle, buf, host, port)
    }

    override fun close() {
        nativeCloseAndDestroy(handle)
    }

    override fun mcastJoin(addr: String, port: Int): Boolean {
        return nativeMcastJoin(handle, addr, port)
    }

    override fun mcastLeave(addr: String, port: Int): Boolean {
        return nativeMcastLeave(handle, addr, port)
    }

    override fun setListener(listener: UdpSocket.Listener.() -> Unit) {
        this.listener = ListenerKt().apply(listener)
    }

    private fun initNativeHandle() {
        if (handle.compareTo(0) == 0) {
            handle = nativeCreate()
        }
    }

    fun onData(arr: ByteArray, host: String, port: Int) {
        listener?.onData(arr, host, port)
    }

    fun onError(err: Int) {
        listener?.onError(err)
    }

    inner class ListenerKt : UdpSocket.Listener {
        private var dataListener: ((arr: ByteArray, host: String, port: Int) -> Unit)? = null
        private var errorListener: ((Int) -> Unit)? = null
        fun onData(arr: ByteArray, host: String, port: Int) {
            dataListener?.invoke(arr, host, port)
        }
        fun onError(err: Int) {
            errorListener?.invoke(err)
        }

        override fun onData(listener: (arr: ByteArray, host: String, port: Int) -> Unit) {
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
    private external fun nativeBind(handle: Long, host: String, port: Int, flags: Int): Boolean
    private external fun nativeConnect(handle: Long, host: String, port: Int): Boolean
    private external fun nativeSendString(handle: Long, str: String, host: String, port: Int): Int
    private external fun nativeSendArray(handle: Long, arr: ByteArray, host: String, port: Int): Int
    private external fun nativeSendBuffer(handle: Long, buf: ByteBuffer, host: String, port: Int): Int
    private external fun nativeClose(handle: Long)
    private external fun nativeMcastJoin(handle: Long, addr: String, port: Int): Boolean
    private external fun nativeMcastLeave(handle: Long, addr: String, port: Int): Boolean
    private external fun nativeDestroy(handle: Long)
    private external fun nativeCloseAndDestroy(handle: Long)
}