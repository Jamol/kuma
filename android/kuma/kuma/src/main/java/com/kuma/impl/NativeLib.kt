package com.kuma.impl

class NativeLib {
    companion object {
        private var isLoaded = false
        fun load(): Boolean {
            if (isLoaded) {
                return true
            }
            try {
                System.loadLibrary("kuma")
                isLoaded = true
            } catch (e: UnsatisfiedLinkError) {
                isLoaded = false
                println(e)
            }
            return isLoaded
        }
    }
}