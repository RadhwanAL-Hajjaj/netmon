import java.util.Properties
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Signing keys stay out of the repository. Copy keystore.properties.example to
// keystore.properties (next to settings.gradle.kts) and point it at your own
// keystore. Without it, debug builds use Android's debug key and release
// builds come out unsigned.
val keystoreFile = rootProject.file("keystore.properties")
val hasKeystore = keystoreFile.exists()
val keystore = Properties()
if (hasKeystore) keystoreFile.inputStream().use { keystore.load(it) }

android {
    namespace = "com.example.netmon"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.example.netmon"
        minSdk = 26
        targetSdk = 34
        // Keep in step with BuildInfo in CrashLog.kt.
        versionCode = 5
        versionName = "1.3.1"
    }

    // With keystore.properties present, debug builds are signed with the same
    // key as release builds, so a build from Android Studio installs over an
    // APK you made earlier without uninstalling it first.
    if (hasKeystore) {
        signingConfigs {
            create("netmon") {
                storeFile = rootProject.file(keystore.getProperty("storeFile"))
                storePassword = keystore.getProperty("storePassword")
                keyAlias = keystore.getProperty("keyAlias")
                keyPassword = keystore.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        debug {
            if (hasKeystore) signingConfig = signingConfigs.getByName("netmon")
        }
        release {
            isMinifyEnabled = false
            if (hasKeystore) signingConfig = signingConfigs.getByName("netmon")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
    }
}
