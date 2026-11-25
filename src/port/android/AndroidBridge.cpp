/**
 * JNI surface for the Android app: menu state, ROM recognition, and extraction.
 *
 * The Android version's touch buttons (TouchOverlay.java, TouchControls.java) are fed into SDL here as one virtual
 * game controller, so they go through libultraship's normal mapping and can be remapped like any pad (every pad is
 * on port 1). The engine's own ImGui touch controls (src/port/ui/TouchControls.cpp) start off on Android.
 *
 * The two extraction entry points take their paths as arguments rather than
 * asking Ship::Context: the launcher calls them before SDL exists.
 */
#ifdef __ANDROID__

#include <jni.h>
#include <vector>
#include <android/log.h>
#include <SDL2/SDL.h>

#include <atomic>
#include <exception>
#include <filesystem>
#include <string>

#include "extractor/GameExtractor.h"
#include "ship/Context.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"
#include <libultraship/bridge/windowbridge.h>

namespace {

constexpr const char* kLogTag = "Paperboat";
constexpr const char* kGameArchive = "pm64.o2r";

std::string ToStdString(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string result = chars != nullptr ? chars : "";
    if (chars != nullptr) {
        env->ReleaseStringUTFChars(value, chars);
    }
    return result;
}

// The touch buttons as an ordinary SDL game controller. A virtual GAMECONTROLLER joystick gets SDL's default
// mapping: button i = SDL_GameControllerButton i, axis i = SDL_GameControllerAxis i.
SDL_Joystick* gTouchJoystick = nullptr;

void AttachTouchJoystick() {
    if (gTouchJoystick != nullptr || !SDL_WasInit(SDL_INIT_JOYSTICK)) {
        return;
    }
    const int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX,
                                                SDL_CONTROLLER_BUTTON_MAX, 0);
    if (index < 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Could not attach the touch buttons: %s", SDL_GetError());
        return;
    }
    gTouchJoystick = SDL_JoystickOpen(index);
    if (gTouchJoystick == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Could not open the touch buttons: %s", SDL_GetError());
        SDL_JoystickDetachVirtual(index);
        return;
    }
    // a trigger at rest is the axis minimum (raw 0 would read as half pressed)
    SDL_JoystickSetVirtualAxis(gTouchJoystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_JOYSTICK_AXIS_MIN);
    SDL_JoystickSetVirtualAxis(gTouchJoystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, SDL_JOYSTICK_AXIS_MIN);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Touch buttons attached as a virtual controller");
}

} // namespace

extern "C" {

/**
 * The touch buttons' state (TouchOverlay bits: 0 A, 1 B, 2 Z, 3 L, 4 R, 5 START, 6-9 C up/down/left/right,
 * 10-13 D-pad up/down/left/right; the stick in -1..1, y down = +), sent the way libultraship's default N64 mapping
 * reads a pad: A = A, B = B, Z = left trigger, L = left shoulder, R = right trigger, START = Start, the C buttons =
 * the right stick, the D-pad = the D-pad.
 */
JNIEXPORT void JNICALL Java_dev_net64_paperboat_TouchNative_nativeTouchPad(JNIEnv*, jclass, jint bits, jfloat x, jfloat y) {
    AttachTouchJoystick();
    if (gTouchJoystick == nullptr) {
        return;
    }
    auto on = [bits](int bit) { return (bits >> bit) & 1; };
    SDL_JoystickSetVirtualButton(gTouchJoystick, SDL_CONTROLLER_BUTTON_A, on(0));
    SDL_JoystickSetVirtualButton(gTouchJoystick, SDL_CONTROLLER_BUTTON_B, on(1));
    SDL_JoystickSetVirtualButton(gTouchJoystick, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, on(3));
    SDL_JoystickSetVirtualButton(gTouchJoystick, SDL_CONTROLLER_BUTTON_START, on(5));
    SDL_JoystickSetVirtualButton(gTouchJoystick, SDL_CONTROLLER_BUTTON_DPAD_UP, on(10));
    SDL_JoystickSetVirtualButton(gTouchJoystick, SDL_CONTROLLER_BUTTON_DPAD_DOWN, on(11));
    SDL_JoystickSetVirtualButton(gTouchJoystick, SDL_CONTROLLER_BUTTON_DPAD_LEFT, on(12));
    SDL_JoystickSetVirtualButton(gTouchJoystick, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, on(13));
    SDL_JoystickSetVirtualAxis(gTouchJoystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT,
                               on(2) ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN);
    SDL_JoystickSetVirtualAxis(gTouchJoystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
                               on(4) ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN);
    const int cy = on(6) ? SDL_JOYSTICK_AXIS_MIN : (on(7) ? SDL_JOYSTICK_AXIS_MAX : 0);
    const int cx = on(8) ? SDL_JOYSTICK_AXIS_MIN : (on(9) ? SDL_JOYSTICK_AXIS_MAX : 0);
    SDL_JoystickSetVirtualAxis(gTouchJoystick, SDL_CONTROLLER_AXIS_RIGHTX, static_cast<Sint16>(cx));
    SDL_JoystickSetVirtualAxis(gTouchJoystick, SDL_CONTROLLER_AXIS_RIGHTY, static_cast<Sint16>(cy));
    SDL_JoystickSetVirtualAxis(gTouchJoystick, SDL_CONTROLLER_AXIS_LEFTX, static_cast<Sint16>(x * SDL_JOYSTICK_AXIS_MAX));
    SDL_JoystickSetVirtualAxis(gTouchJoystick, SDL_CONTROLLER_AXIS_LEFTY, static_cast<Sint16>(y * SDL_JOYSTICK_AXIS_MAX));
}

/** The game runs (libultraship's window exists): the touch buttons may show. */
JNIEXPORT jboolean JNICALL Java_dev_net64_paperboat_TouchNative_nativeGameRunning(JNIEnv*, jclass) {
    auto context = Ship::Context::GetRawInstance();
    return (context != nullptr && context->GetWindow() != nullptr) ? JNI_TRUE : JNI_FALSE;
}

/**
 * Whether libultraship's menu is up. A keyboard, a gamepad or the menu itself
 * can change this behind Kotlin's back, so ask the engine rather than mirror it.
 *
 * Races with the game thread by design: it decides whether one button shows,
 * and a torn read self-corrects on the next poll.
 */
JNIEXPORT jboolean JNICALL Java_dev_net64_paperboat_MainActivity_isMenuOpen(JNIEnv*, jobject) {
    auto context = Ship::Context::GetRawInstance();
    if (context == nullptr) {
        return JNI_FALSE;
    }

    auto window = context->GetWindow();
    if (window == nullptr) {
        return JNI_FALSE;
    }

    auto gui = window->GetGui();
    if (gui == nullptr) {
        return JNI_FALSE;
    }

    return gui->GetMenuOrMenubarVisible() ? JNI_TRUE : JNI_FALSE;
}

/**
 * The name config.yml gives the ROM at romPath, or null if it has no recipe.
 * Reading the same config.yml the extraction does keeps the launcher from
 * carrying its own copy of the supported hashes.
 */
JNIEXPORT jstring JNICALL
Java_dev_net64_paperboat_GameAssets_nativeDetectRom(JNIEnv* env, jobject, jstring jRomPath, jstring jSourceDir) {
    const std::string romPath = ToStdString(env, jRomPath);
    const std::string sourceDir = ToStdString(env, jSourceDir);

    const auto version = GameExtractor::DetectVersion(romPath, sourceDir);
    if (!version.has_value()) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "Unrecognised ROM at %s", romPath.c_str());
        return nullptr;
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Recognised %s as %s", romPath.c_str(), version->c_str());
    return env->NewStringUTF(version->c_str());
}

/**
 * Runs Torch over the ROM at romPath and writes pm64.o2r into destDir.
 * sourceDir holds config.yml and assets/, unpacked from the APK by the
 * launcher. Returns null on success, or a message describing the failure.
 */
JNIEXPORT jstring JNICALL Java_dev_net64_paperboat_GameAssets_nativeGenerateGameArchive(
    JNIEnv* env, jobject, jstring jRomPath, jstring jSourceDir, jstring jDestDir
) {
    const std::string romPath = ToStdString(env, jRomPath);
    const std::string sourceDir = ToStdString(env, jSourceDir);
    const std::string destDir = ToStdString(env, jDestDir);

    const auto fail = [env](const std::string& message) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Asset generation failed: %s", message.c_str());
        return env->NewStringUTF(message.c_str());
    };

    GameExtractor extractor;
    if (!extractor.RunStandalone(romPath, sourceDir)) {
        return fail("Could not read a supported Paper Mario ROM at " + romPath + ".");
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Extracting %s into %s", romPath.c_str(), destDir.c_str());

    std::string extractError;
    std::atomic<size_t> assetCount { 0 };
    std::atomic<size_t> totalAssets { 0 };
    try {
        if (!extractor.GenerateOTRTo(assetCount, totalAssets, sourceDir, destDir)) {
            extractError = GameExtractor::sLastError.empty()
                ? "Torch could not extract the ROM."
                : "Torch could not extract the ROM: " + GameExtractor::sLastError;
        }
    } catch (const std::exception& error) {
        extractError = std::string("Torch could not extract the ROM: ") + error.what();
    } catch (...) {
        extractError = "Torch could not extract the ROM.";
    }

    if (!extractError.empty()) {
        return fail(extractError);
    }

    // Init() logs and returns rather than throwing for some failures, so
    // confirm the archive really landed.
    std::error_code error;
    const std::filesystem::path archive = std::filesystem::path(destDir) / kGameArchive;
    if (!std::filesystem::exists(archive, error) || std::filesystem::file_size(archive, error) == 0) {
        return fail(
            std::string("Torch finished without producing ") + kGameArchive
            + ". Check that the ROM is Paper Mario (N64, US)."
        );
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Wrote %s", archive.c_str());
    return nullptr;
}

} // extern "C"

#endif // __ANDROID__

// The Mods tab of the settings menu (ui/Menu.cpp): MainActivity.openModsFromMenu() opens the mods and saves manager.
namespace Ship {
void Android_OpenModsManager() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(cls, "openModsFromMenu", "()V");
    if (method != nullptr) {
        env->CallVoidMethod(activity, method);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}

// The card menu's view of the Back menu's pages (TouchControls' bridge* methods, MenuCards.cpp): the class is found
// through the activity's class loader (FindClass on this thread only sees the system classes) and kept.
static jclass TouchControlsClass(JNIEnv* env) {
    static jclass cls = nullptr;
    if (cls != nullptr) {
        return cls;
    }
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (activity == nullptr) {
        return nullptr;
    }
    jclass actCls = env->GetObjectClass(activity);
    jmethodID getLoader = env->GetMethodID(actCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = env->CallObjectMethod(activity, getLoader);
    jclass loaderCls = env->FindClass("java/lang/ClassLoader");
    jmethodID load = env->GetMethodID(loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = env->NewStringUTF("dev.net64.paperboat.TouchControls");
    jobject found = env->CallObjectMethod(loader, load, name);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        found = nullptr;
    }
    if (found != nullptr) {
        cls = static_cast<jclass>(env->NewGlobalRef(found));
        env->DeleteLocalRef(found);
    }
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(loaderCls);
    env->DeleteLocalRef(loader);
    env->DeleteLocalRef(actCls);
    env->DeleteLocalRef(activity);
    return cls;
}

std::vector<std::string> Android_MenuPage(const char* id) {
    std::vector<std::string> out;
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env ? TouchControlsClass(env) : nullptr;
    if (cls == nullptr) {
        return out;
    }
    static jmethodID m = env->GetStaticMethodID(cls, "bridgeItems", "(Ljava/lang/String;)[Ljava/lang/String;");
    jstring jid = env->NewStringUTF(id);
    auto arr = static_cast<jobjectArray>(env->CallStaticObjectMethod(cls, m, jid));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        arr = nullptr;
    }
    if (arr != nullptr) {
        const jsize n = env->GetArrayLength(arr);
        for (jsize i = 0; i < n; i++) {
            auto js = static_cast<jstring>(env->GetObjectArrayElement(arr, i));
            const char* c = js ? env->GetStringUTFChars(js, nullptr) : nullptr;
            out.emplace_back(c ? c : "");
            if (c) env->ReleaseStringUTFChars(js, c);
            if (js) env->DeleteLocalRef(js);
        }
        env->DeleteLocalRef(arr);
    }
    env->DeleteLocalRef(jid);
    return out;
}

void Android_MenuAct(const char* id, int index, int op) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env ? TouchControlsClass(env) : nullptr;
    if (cls == nullptr) {
        return;
    }
    static jmethodID m = env->GetStaticMethodID(cls, "bridgeAct", "(Ljava/lang/String;II)V");
    jstring jid = env->NewStringUTF(id);
    env->CallStaticVoidMethod(cls, m, jid, index, op);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(jid);
}

std::string Android_MenuTakePushed() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env ? TouchControlsClass(env) : nullptr;
    if (cls == nullptr) {
        return "";
    }
    static jmethodID m = env->GetStaticMethodID(cls, "bridgeTakePushed", "()Ljava/lang/String;");
    auto js = static_cast<jstring>(env->CallStaticObjectMethod(cls, m));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    std::string r;
    if (js != nullptr) {
        const char* c = env->GetStringUTFChars(js, nullptr);
        r = c ? c : "";
        env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
    }
    return r;
}

bool Android_MenuTakeClose() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env ? TouchControlsClass(env) : nullptr;
    if (cls == nullptr) {
        return false;
    }
    static jmethodID m = env->GetStaticMethodID(cls, "bridgeTakeClose", "()Z");
    const bool r = env->CallStaticBooleanMethod(cls, m);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return r;
}
} // namespace Ship

// ---------------------------------------------------------------- FPS / frame time (the Back menu's readout)
#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <mutex>
// frame_stats.inc - frame statistics for the Back menu's FPS / frame time readout (TouchControls, FpsView): tick()
// once per frame shown; read() gives fps, average and longest frame time (ms) of the last half second.
namespace frame_stats {
struct State {
    std::mutex mutex;
    int64_t last = 0, windowStart = 0;
    int frames = 0;
    double sum = 0, longest = 0;
    float fps = 0, avg = 0, max = 0;
};
inline State& state() { static State s; return s; }
inline int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void tick() {
    State& s = state();
    const int64_t now = nowNs();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.last != 0) {
        const double dt = (now - s.last) / 1e6;
        s.frames++;
        s.sum += dt;
        s.longest = std::max(s.longest, dt);
    }
    s.last = now;
    if (s.windowStart == 0) s.windowStart = now;
    if (now - s.windowStart >= 500000000LL && s.frames > 0) {
        s.fps = static_cast<float>(s.frames * 1e9 / (now - s.windowStart));
        s.avg = static_cast<float>(s.sum / s.frames);
        s.max = static_cast<float>(s.longest);
        s.frames = 0;
        s.sum = s.longest = 0;
        s.windowStart = now;
    }
}
inline void read(float out[3]) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    const bool stale = s.last == 0 || nowNs() - s.last > 1000000000LL;   // no frame for a second: 0 fps
    out[0] = stale ? 0.0f : s.fps;
    out[1] = s.avg;
    out[2] = s.max;
}
} // namespace frame_stats

// Every frame the game shows goes through SDL_GL_SwapWindow (libultraship's SDL backend). Defined here, it takes
// libultraship's calls (static, in this library) before libSDL2.so's, counts the frame and hands on to SDL.
extern "C" void SDL_GL_SwapWindow(SDL_Window* window) {
    using Swap = void (*)(SDL_Window*);
    static Swap real = reinterpret_cast<Swap>(dlsym(dlopen("libSDL2.so", RTLD_NOW | RTLD_NOLOAD), "SDL_GL_SwapWindow"));
    frame_stats::tick();
    if (real != nullptr) {
        real(window);
    }
}

extern "C" JNIEXPORT jfloatArray JNICALL Java_dev_net64_paperboat_TouchNative_nativeFrameStats(JNIEnv* env, jclass) {
    float s[3];
    frame_stats::read(s);
    jfloatArray a = env->NewFloatArray(3);
    env->SetFloatArrayRegion(a, 0, 3, s);
    return a;
}

// Android's Back while the menu is open (MainActivity.toggleGameMenu): one level up in the card menu, the next frame.
namespace Ship { extern std::atomic<bool> gMenuBackRequest; }
extern "C" JNIEXPORT void JNICALL Java_dev_net64_paperboat_MainActivity_nativeMenuBack(JNIEnv*, jobject) {
    Ship::gMenuBackRequest = true;
}
