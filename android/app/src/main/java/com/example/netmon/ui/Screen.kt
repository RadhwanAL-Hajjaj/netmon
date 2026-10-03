package com.example.netmon.ui

import android.content.Context
import android.view.View
import com.example.netmon.Board
import com.example.netmon.MainActivity

/** One destination of the bottom bar. Built once, re-rendered from Board whenever a reading lands. */
abstract class Screen(val host: MainActivity) {

    val ctx: Context get() = host

    /** What to read from the board while this screen is showing. */
    abstract val parts: List<Board.Part>

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

    /** True when the back key was handled here. */
    open fun onBack(): Boolean = false
}
