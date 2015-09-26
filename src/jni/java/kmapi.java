package com.jamol.kuma;

import android.os.Handler;
import android.os.Looper;

public final class kmapi {
    static {
        System.loadLibrary("c++_shared");
        System.loadLibrary("kuma");
    }

    private static Handler mHandler;

    public static void init() {
        mHandler = new Handler(Looper.getMainLooper());
    }

    public static void runOnMainThread(Runnable r) {
        mHandler.post(r);
    }
}
