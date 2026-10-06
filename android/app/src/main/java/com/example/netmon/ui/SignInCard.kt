package com.example.netmon.ui

import android.view.View
import android.widget.TextView
import android.widget.Toast
import com.example.netmon.Auth
import com.example.netmon.Board
import com.example.netmon.MainActivity

/**
 * Settings, Signing in (firmware 0.13): whether this phone is signed in to the
 * board, whether its password is saved here, and signing out.
 *
 * The login password itself is set on the board's own Settings page: there it
 * can sign out every other browser and phone at the same time, which is what
 * a new password is usually for.
 */
class SignInCard(private val host: MainActivity) {

    private lateinit var state: TextView
    private lateinit var savedLine: KV
    private lateinit var passwordLine: KV
    private lateinit var sessionsLine: KV
    private lateinit var outBtn: TextView
    private lateinit var forgetBtn: TextView
    private lateinit var msg: TextView
    private var busy = false

    fun build(): View {
        val c = host
        val card = c.card()
        card.add(c.cardTitle("Signing in"))
        state = card.add(c.label("", 14f, T.TEXT2), top = 6)
        savedLine = card.addKV("Password saved", top = 10)
        passwordLine = card.addKV("Signs in with")
        sessionsLine = card.addKV("Signed in now")
        val r = card.add(c.row(), top = 14)
        outBtn = r.add(c.button("Sign out", Btn.SECONDARY) { confirmSignOut() }, 0, WRAP, weight = 1f)
        forgetBtn = r.add(c.button("Forget password", Btn.SECONDARY) { forgetPassword() }, 0, WRAP, weight = 1f, start = 8)
        msg = card.add(c.label("", 14f, T.TEXT2), top = 8)
        msg.visibility = View.GONE
        card.add(c.hint("The monitor's login password is set on its own Settings page, under Signing in, which can " +
            "also sign out every browser and phone at once. The update password from secrets.h always signs in too."), top = 10)
        return card
    }

    private fun say(text: String, color: Int = T.TEXT2) {
        msg.text = text
        msg.setTextColor(color)
        msg.visibility = if (text.isEmpty()) View.GONE else View.VISIBLE
    }

    fun render() {
        if (!::state.isInitialized) return
        val a = Board.auth
        val old = Board.missing(Board.Part.AUTH)
        val saved = Auth.hasSavedPassword()
        state.put(when {
            old -> "This monitor's firmware does not ask for a password. From firmware 0.13 it does."
            Board.needsLogin -> "Not signed in. Tap Sign in at the top to sign in."
            a == null -> "Reading"
            saved -> "Signed in. The password is saved on this phone, so the app signs in again by itself when the monitor asks."
            else -> "Signed in for ${a.rememberDays} days. The password is not saved: the app asks for it when the monitor does."
        }, if (Board.needsLogin && !old) T.WARN else T.TEXT2)
        val known = a != null && !old
        savedLine.show(!old)
        savedLine.set(if (saved) "Yes, encrypted, for this app only" else "No")
        passwordLine.show(known)
        sessionsLine.show(known)
        if (a != null) {
            passwordLine.set(if (a.ownPassword) "The login password, or the update password" else "The update password")
            sessionsLine.set("${a.sessions} " + if (a.sessions == 1) "browser or phone" else "browsers and phones")
        }
        outBtn.shown(!old && !Board.needsLogin)
        outBtn.enabled(!busy)
        forgetBtn.shown(saved)
        forgetBtn.enabled(!busy)
    }

    private fun confirmSignOut() {
        host.dialog("Sign out of the monitor?",
            host.label("This phone's session ends and the saved password is forgotten. The app asks for the " +
                "password next time it reaches the monitor.", 15f, T.TEXT2),
            "Sign out", { signOut() })
    }

    private fun signOut() {
        busy = true
        render()
        Board.signOut {
            busy = false
            say("Signed out.", T.OK)
            render()
        }
    }

    private fun forgetPassword() {
        Auth.forgetSavedPassword()
        say("The password is no longer saved on this phone. The app stays signed in until the session ends.", T.OK)
        render()
        Toast.makeText(host, "Password forgotten", Toast.LENGTH_SHORT).show()
    }
}
