plugins {
    java
    application
    id("org.openjfx.javafxplugin") version "0.0.13"
}

repositories {
    mavenCentral()
}

dependencies {
    implementation("net.java.dev.jna:jna:5.14.0")
    implementation("net.java.dev.jna:jna-platform:5.14.0")
    testImplementation("org.junit.jupiter:junit-jupiter:5.10.1")
}

tasks.test {
    useJUnitPlatform()
}

javafx {
    version = "21.0.1"
    modules = listOf("javafx.controls", "javafx.graphics")
}

application {
    mainClass.set("onenode.spike.SpikeApp")
}
