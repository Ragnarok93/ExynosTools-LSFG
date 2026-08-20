plugins {
    id("com.android.application")
}

android {
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
            version = "3.22.1"
        }
    }

    sourceSets["main"].jniLibs.srcDir("../../../../build-lsfg")
}

