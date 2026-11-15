plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "app.onenode"
    compileSdk = 35

    defaultConfig {
        applicationId = "app.onenode"
        minSdk = 29          // Android 10: the first version whose system TLS speaks TLS 1.3 (OneNode needs it)
        targetSdk = 35
        versionCode = (System.getenv("ONENODE_VERSION_CODE") ?: "1").toInt()
        versionName = System.getenv("ONENODE_VERSION_NAME") ?: "0.1.0"
        resourceConfigurations += "en" // the app is English only: no other languages' texts of the libraries in the APK
    }

    // A release APK is signed with your own key when the four variables below are set (the release workflow does
    // this from GitHub secrets). Without them it is signed with the debug key: fine for trying it on your phone,
    // not for publishing in a store.
    signingConfigs {
        val keystore = System.getenv("ONENODE_KEYSTORE")
        if (keystore != null) {
            create("release") {
                storeFile = file(keystore)
                storePassword = System.getenv("ONENODE_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("ONENODE_KEY_ALIAS")
                keyPassword = System.getenv("ONENODE_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true       // R8: removes unused code, the APK stays small
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
        buildConfig = false
    }

    packaging {
        resources.excludes += setOf("/META-INF/{AL2.0,LGPL2.1}", "/META-INF/*.version", "/kotlin/**", "DebugProbesKt.bin")
    }

    lint {
        warningsAsErrors = false
        abortOnError = true
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation(project(":core"))
    implementation(platform(libs.compose.bom))
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.compose.ui)
    implementation(libs.compose.foundation)
    implementation(libs.compose.material3)
    testImplementation(libs.junit)
}
