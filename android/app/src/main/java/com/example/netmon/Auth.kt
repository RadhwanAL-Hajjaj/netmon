package com.example.netmon

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * Signing in to the board (firmware 0.13).
 *
 * Every request carries the session the board handed out, over Wi-Fi and over
 * Bluetooth alike. The app always asks for a session that lasts 30 days. When
 * one ends (its 30 days are up, "Sign out everywhere", a new login password),
 * the password saved on this phone gets a new one without asking anybody, and
 * only without a saved password, or with one the board no longer takes, does
 * the app ask.
 *
 * The session and the saved password belong to one board, known by its chip's
 * own MAC: an address not yet known to be that board is never sent the
 * session, and the password goes only to a board that says it is that one.
 */
object Auth {

    private val lock = Any()

    /**
     * The board wants a password the app cannot supply by itself: none is
     * saved, the saved one is no longer right, or this is another board.
     */
    @Volatile var needed = false
        private set

    fun token(): String? = AppState.prefs.sessionToken

    fun hasSavedPassword(): Boolean = AppState.prefs.savedPassword != null

    /** A client for the board the app uses: its session, and a new one when that ends. */
    fun client(base: String): NetmonClient {
        val c = NetmonClient(base)
        c.token = token()
        c.renewSession = renewer
        return c
    }

    private val renewer: (NetmonClient) -> String? = { c -> renew(c) }

    /**
     * A new session with the saved password, for a request the board turned
     * away. Requests on two threads at once get one session between them.
     * Blocking: worker threads only.
     */
    fun renew(c: NetmonClient): String? {
        synchronized(lock) {
            return renewLocked(c)
        }
    }

    private fun renewLocked(c: NetmonClient): String? {
        val p = AppState.prefs
        val now = p.sessionToken
        // Another request got one meanwhile.
        if (now != null && now != c.token) return now
        p.sessionToken = null
        val pw = p.savedPassword?.let { Sealed.open(it) }
        if (pw == null) {
            needed = true
            return null
        }
        val who = c.identify() ?: return null          // not answering: try again later
        val owner = p.sessionFor
        if (!who.login || (owner != null && !who.id.equals(owner, ignoreCase = true))) {
            needed = true
            return null
        }
        return try {
            val r = c.login(pw, remember = true)
            p.sessionToken = r.token
            needed = false
            r.token
        } catch (e: ApiException) {
            if (e.kind == ApiException.Kind.Unauthorized) {
                // The password was changed on the board: the saved one is no use now.
                p.savedPassword = null
                needed = true
            }
            null
        }
    }

    /**
     * Signs in with [password] typed by the person, and keeps the session,
     * and the password too when [save]. Blocking. Throws ApiException: a
     * wrong password is Unauthorized, too many tries Http 429.
     */
    fun signIn(c: NetmonClient, password: String, save: Boolean): AuthInfo {
        val who = c.identify()
            ?: throw ApiException(ApiException.Kind.Unreachable, "The monitor did not answer.")
        if (!who.login) {
            throw ApiException(ApiException.Kind.Http, "This monitor's firmware does not ask for a password.", 404)
        }
        val r = c.login(password, remember = true)
        val p = AppState.prefs
        synchronized(lock) {
            p.sessionToken = r.token
            p.sessionFor = who.id.ifEmpty { null }
            p.savedPassword = if (save) Sealed.seal(password) else null
            needed = false
        }
        c.token = r.token
        return c.auth()
    }

    @Volatile private var saveWorks: Boolean? = null

    /** True when "Save password" works on this phone: tried once, the first time it is asked. */
    fun canSave(): Boolean = saveWorks ?: (Sealed.seal("check") != null).also { saveWorks = it }

    /** Signed out on purpose: the session and the saved password go. */
    fun signedOut() {
        synchronized(lock) {
            val p = AppState.prefs
            p.sessionToken = null
            p.savedPassword = null
            needed = true
        }
    }

    fun forgetSavedPassword() {
        AppState.prefs.savedPassword = null
    }

    /** Another board, or none: nothing of this one's goes to it. */
    fun forgetBoard() {
        synchronized(lock) {
            val p = AppState.prefs
            p.sessionToken = null
            p.sessionFor = null
            p.savedPassword = null
            needed = false
        }
    }

    /** A new address, not yet known to be this board: it gets no session until it is. */
    fun dropSession() {
        synchronized(lock) {
            AppState.prefs.sessionToken = null
            needed = false
        }
    }

    /**
     * The password, encrypted with an AES key that never leaves Android's
     * keystore. Without the keystore (some old or unusual phones) nothing is
     * saved, and the app asks for the password when the board does.
     */
    private object Sealed {
        private const val ALIAS = "netmon-saved-password"
        private const val GCM_BITS = 128

        private fun key(): SecretKey {
            val ks = KeyStore.getInstance("AndroidKeyStore")
            ks.load(null)
            (ks.getKey(ALIAS, null) as? SecretKey)?.let { return it }
            val g = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
            g.init(KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build())
            return g.generateKey()
        }

        fun seal(plain: String): String? = try {
            val c = Cipher.getInstance("AES/GCM/NoPadding")
            c.init(Cipher.ENCRYPT_MODE, key())
            val ct = c.doFinal(plain.toByteArray(Charsets.UTF_8))
            Base64.encodeToString(c.iv, Base64.NO_WRAP) + ":" + Base64.encodeToString(ct, Base64.NO_WRAP)
        } catch (e: Exception) {
            null
        }

        fun open(sealed: String): String? = try {
            val i = sealed.indexOf(':')
            val iv = Base64.decode(sealed.substring(0, i), Base64.NO_WRAP)
            val ct = Base64.decode(sealed.substring(i + 1), Base64.NO_WRAP)
            val c = Cipher.getInstance("AES/GCM/NoPadding")
            c.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(GCM_BITS, iv))
            String(c.doFinal(ct), Charsets.UTF_8)
        } catch (e: Exception) {
            null
        }
    }
}
