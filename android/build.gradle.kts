// No AndroidX or other libraries: the app uses only the Android framework and
// the Kotlin standard library, so it can also be built without Gradle.
plugins {
    id("com.android.application") version "8.7.3" apply false
    id("org.jetbrains.kotlin.android") version "2.1.21" apply false
}
