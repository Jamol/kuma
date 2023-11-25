package com.kuma.kmapi

import java.nio.ByteBuffer

interface WebSocket {
    fun open(url: String): Boolean
    fun send(str: String): Int
    fun send(arr: ByteArray): Int
    fun send(buf: ByteBuffer): Int
    fun close()
}