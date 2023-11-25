package com.kuma.impl

class NativeLib {
    companion object {
        // Used to load the 'kuma' library on application startup.
        init {
            System.loadLibrary("kuma")
        }
    }
}