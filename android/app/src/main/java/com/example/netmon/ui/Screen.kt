package com.example.netmon.ui

import android.content.Context
import android.view.View
import com.example.netmon.Board
import com.example.netmon.MainActivity

/** One destination of the bottom bar. Built once, re-rendered from Board whenever a reading lands. */
abstract class Screen(val host: MainActivity) {

    val ctx: Context get() = host

    /** What to read from the board every ten seconds while this screen is showing. */
    abstract val parts: List<Board.Part>

    /**
     * What to read more often than that, every [fastMs], such as the Nearby
     * tables: reading them is what keeps the board's scans quick. Empty for none.
     */
    open val fastParts: List<Board.Part> get() = emptyList()
    open val fastMs: Long get() = 0L

    private var built: View? = null

    val view: View
        get() = built ?: build().also {
            built = it
            render()
        }

    protected abstract fun build(): View

    /** Update every view from the latest readings. Cheap, and safe to call at any time. */
    abstract fun render()

    open fun onShown() {}

    open fun onHidden() {}

    /** The app went to the background (or came back) while this screen was showing. */
    open fun onPause() {}

    open fun onResume() {}

    /** True when the back key was handled here. */
    open fun onBack(): Boolean = false
}
