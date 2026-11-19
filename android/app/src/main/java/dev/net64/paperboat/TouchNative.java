// TouchNative - the JNI side of the touch buttons (src/port/android/AndroidBridge.cpp in libmain.so).
package dev.net64.paperboat;

final class TouchNative {
    private TouchNative() { }

    /** TouchOverlay's button bits and its stick (-1..1, y down = +), sent as a virtual SDL game controller. */
    static native void nativeTouchPad(int buttons, float x, float y);
    /** The game runs (libultraship's window exists). */
    static native boolean nativeGameRunning();
}
