package com.kuma.impl

import java.security.KeyStore
import java.util.Base64
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager

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
            if (isLoaded) {
                loadSystemTrustStore()
            }
            return isLoaded
        }
        private fun loadSystemTrustStore() {
            val tmf = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
            tmf.init(null as KeyStore?)
            var trustManager: X509TrustManager? = null
            for (tm in tmf.trustManagers) {
                if (tm is X509TrustManager) {
                    trustManager = tm
                    break
                }
            }
            var caCerts = ""
            trustManager?.let { tm ->
                for (cert in tm.acceptedIssuers) {
                    val encodedCert = Base64.getEncoder().encodeToString(cert.encoded)
                    val pem = buildString {
                        append("-----BEGIN CERTIFICATE-----\n")
                        append(encodedCert.chunked(64).joinToString("\n"))
                        append("\n-----END CERTIFICATE-----\n")
                    }
                    caCerts += pem
                }
            }
            nativeLibInit(caCerts)
        }
        private external fun nativeLibInit(caCerts: String)
    }
}