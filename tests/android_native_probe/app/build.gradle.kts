plugins {
    id("com.android.application")
}

android {
    ndkVersion = "27.2.12479018"

    namespace = "com.exynostools.androidprobe"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.exynostools.androidprobe"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1-phase3c"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    sourceSets["main"].jniLibs.srcDir("src/main/jniLibs")
}
