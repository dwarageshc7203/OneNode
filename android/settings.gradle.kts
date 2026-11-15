// OneNode Android: two modules.
//   :core = plain Kotlin/JVM library. The protocol, pairing, TLS sessions and trust list. No Android classes,
//           so it is tested on any computer (and the same tests run in CI without a phone).
//   :app  = the Android app (screens, foreground service, Keystore identity, file saving, device discovery).
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "0.10.0"
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "OneNodeAndroid"
include(":core", ":app")
