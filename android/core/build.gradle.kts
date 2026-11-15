// :core is plain Kotlin on the JVM. On Android, org.json is part of the system, so it is "compileOnly" here
// (not shipped inside the app); the unit tests on a normal computer need the real library, hence testImplementation.
plugins {
    alias(libs.plugins.kotlin.jvm)
}

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
        allWarningsAsErrors.set(true)
    }
}

dependencies {
    compileOnly(libs.org.json)
    testImplementation(libs.org.json)
    testImplementation(libs.junit)
    testImplementation(libs.conscrypt) // the TLS library Android itself uses: lets the tests check our TLS code against it
}

tasks.test {
    // the interop tests talk to the desktop program; they run only when ONENODE_CLI points to it (see InteropTest)
    environment("ONENODE_CLI", System.getenv("ONENODE_CLI") ?: "")
    environment("LC_ALL", "C.UTF-8") // so the file system accepts names like "写真.txt" in the tests
    testLogging { events("failed", "skipped"); exceptionFormat = org.gradle.api.tasks.testing.logging.TestExceptionFormat.FULL }
}

// The same tests again, but with Conscrypt as the TLS provider instead of the JDK's own. Android's system TLS IS Conscrypt,
// so this is the closest check of the TLS code that a normal computer can do (the Android Keystore itself is not covered).
val testConscrypt by tasks.registering(Test::class) {
    description = "Runs the tests with Conscrypt (Android's TLS library) as the TLS provider"
    group = "verification"
    testClassesDirs = sourceSets.test.get().output.classesDirs
    classpath = sourceSets.test.get().runtimeClasspath
    systemProperty("onenode.provider", "conscrypt")
    // the desktop build of Conscrypt peeks into java.net.InetAddress, which a modern JDK locks; Android's own copy does not need this
    jvmArgs("--add-opens", "java.base/java.net=ALL-UNNAMED")
    environment("ONENODE_CLI", System.getenv("ONENODE_CLI") ?: "")
    environment("LC_ALL", "C.UTF-8")
    testLogging { events("failed", "skipped"); exceptionFormat = org.gradle.api.tasks.testing.logging.TestExceptionFormat.FULL }
}
tasks.check { dependsOn(testConscrypt) }
