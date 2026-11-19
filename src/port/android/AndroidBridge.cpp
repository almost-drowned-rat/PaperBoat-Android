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
} // namespace Ship
