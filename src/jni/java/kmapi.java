package com.jamol.kuma;

public final class kmapi {
    static {
        System.loadLibrary("c++_shared");
        System.loadLibrary("kuma");
    }

    public static void load() {

    }
}
