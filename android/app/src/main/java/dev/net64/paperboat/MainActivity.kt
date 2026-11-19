package dev.net64.paperboat

import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.libsdl.app.SDLActivity
import org.libsdl.app.SDLSurface

/**
 * The game. Over SDL's surface: the touch buttons (TouchControls / TouchOverlay, as in this fork's other Android
 * ports; Automatic hides them while a controller is used, the icon at the top centre opens their settings) and a
 * Mods button, shown while the engine's menu is up. Android's Back opens and closes the engine's menu.
 *
 * [LauncherActivity] guarantees pm64.o2r exists before this activity starts.
 */
class MainActivity : SDLActivity() {

    private lateinit var modsButton: Button

    private var menuOpen = false

    private var touch: TouchControls? = null

    /** RetroArch shaders: the packs and the choice (ShaderLibrary), drawn by ShaderHost through ShaderSurface. */
    private var shaders: ShaderLibrary? = null

    private val handler = Handler(Looper.getMainLooper())
    private val menuWatcher = object : Runnable {
        override fun run() {
            syncMenuState()
            handler.postDelayed(this, MENU_POLL_MS)
        }
    }

    // org/libsdl/app stays byte-identical to the SDL release libultraship
    // pins — SDLActivity refuses to start if the two disagree on version — so
    // the library is named here rather than patched in there.
    override fun getLibraries(): Array<String> = arrayOf("SDL2", "main")

    private external fun isMenuOpen(): Boolean

    /** With a shader chosen, SDL draws into the shader presenter's buffer (ShaderSurface) instead of the screen. */
    override fun createSDLSurface(context: Context): SDLSurface =
        if (ShaderHost.wanted(context)) ShaderSurface(context) else super.createSDLSurface(context)

    @Deprecated("Activity result API of the platform")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (shaders?.onActivityResult(requestCode, resultCode, data) == true) return
        super.onActivityResult(requestCode, resultCode, data)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        goFullscreen()
        addModsButton()
        touch = TouchControls(this, findViewById(android.R.id.content),
            TouchOverlay.Sink { b, x, y -> TouchNative.nativeTouchPad(b, x, y) },
            object : TouchControls.GameState {
                override fun running() = TouchNative.nativeGameRunning()
                override fun menuOpen() = runCatching { isMenuOpen() }.getOrDefault(false)
                override fun openGameMenu() = toggleGameMenu()
            })
        touch?.let { t -> shaders = ShaderLibrary(this, t, ShaderHost.TARGET).also { t.setShaderMenu(it) } }
        // Back opens the menu (TouchControls) or, with the engine's menu open, closes that. From Android 13 the
        // back gesture arrives only through this callback (from Android 16, with targetSdk 36, never as a key).
        if (Build.VERSION.SDK_INT >= 33) {
            onBackInvokedDispatcher.registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT) { onBackMenu() }
        }
    }

    /** The engine's menu opens and closes on Escape. */
    private fun toggleGameMenu() {
        onNativeKeyDown(KeyEvent.KEYCODE_ESCAPE)
        onNativeKeyUp(KeyEvent.KEYCODE_ESCAPE)
    }

    /** Back: the menu (TouchControls) takes it while the game runs; with the engine's menu open it closes that. */
    private fun onBackMenu() {
        if (touch?.onBack() == true) return
        if (runCatching { isMenuOpen() }.getOrDefault(false)) toggleGameMenu()
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (touch?.onKey(event) == true) return true
        if (event.keyCode == KeyEvent.KEYCODE_BACK) {
            if (event.action == KeyEvent.ACTION_UP && runCatching { isMenuOpen() }.getOrDefault(false)) toggleGameMenu()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean =
        touch?.onMotion(event) == true || super.dispatchGenericMotionEvent(event)

    override fun dispatchTouchEvent(event: MotionEvent): Boolean =
        touch?.onTouch(event) == true || super.dispatchTouchEvent(event)

    /**
     * Edge to edge, bars hidden. From API 35 the old fullscreen window flags are
     * ignored, so the bars go through the insets controller instead.
     * Transient-by-swipe keeps a stray swipe from resizing the window and
     * forcing the engine to rebuild its framebuffers.
     */
    private fun goFullscreen() {
        WindowCompat.setDecorFitsSystemWindows(window, false)

        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }

        // Render into the cutout too, rather than letterboxing beside it.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        // The bars return on any focus loss; put them away again coming back.
        if (hasFocus) {
            goFullscreen()
        }
    }

    override fun onResume() {
        super.onResume()
        // Also re-sync now, so returning here never shows a stale menu state.
        syncMenuState()
        handler.removeCallbacks(menuWatcher)
        handler.postDelayed(menuWatcher, MENU_POLL_MS)
        touch?.onResume()
    }

    override fun onPause() {
        handler.removeCallbacks(menuWatcher)
        touch?.onPause()
        super.onPause()
    }

    override fun onDestroy() {
        handler.removeCallbacks(menuWatcher)
        touch?.destroy()
        super.onDestroy()
    }

    /**
     * The menu can be opened by its own button, a keyboard or a gamepad, and can
     * close itself, so ask the engine rather than track it here.
     */
    private fun syncMenuState() {
        val open = runCatching { isMenuOpen() }.getOrDefault(false)
        if (open == menuOpen) return

        menuOpen = open
        // the settings menu has a Mods tab now (openModsFromMenu), so the floating button stays hidden
        modsButton.visibility = android.view.View.GONE
    }

    /** Bottom-left, clear of the engine's menu toggle and of the menu itself. */
    private fun addModsButton() {
        val margin = (16 * resources.displayMetrics.density).toInt()

        modsButton = Button(this).apply {
            text = getString(R.string.mods_button)
            textSize = 12f
            isAllCaps = false
            visibility = android.view.View.GONE
            setOnClickListener { startActivity(Intent(context, ModsActivity::class.java)) }
        }

        val layout = FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.BOTTOM or Gravity.START
        ).apply { setMargins(margin, margin, margin, margin) }

        findViewById<ViewGroup>(android.R.id.content).addView(modsButton, layout)
    }

    private companion object {
        /** Fast enough to feel immediate on a menu toggle, cheap enough to ignore. */
        const val MENU_POLL_MS = 150L
    }

    /** The settings menu's Mods tab (src/port/ui/Menu.cpp, through AndroidBridge.cpp; any thread). */
    @Suppress("unused")
    fun openModsFromMenu() {
        runOnUiThread { startActivity(Intent(this, ModsActivity::class.java)) }
    }
}
