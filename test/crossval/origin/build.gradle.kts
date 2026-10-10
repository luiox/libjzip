plugins {
    java
    application
}

val lljzipVersion = "2.9.1"

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(22)
    }
}

application {
    mainClass.set("jzip.crossval.ZipDump")
}

tasks.withType<JavaCompile> {
    options.compilerArgs.add("--enable-preview")
}

tasks.withType<JavaExec> {
    jvmArgs = (jvmArgs ?: emptyList()) + "--enable-preview"
}

repositories {
    mavenCentral()
    maven { url = uri("https://jitpack.io") }
}

dependencies {
    implementation("com.github.Col-E:LL-Java-Zip:$lljzipVersion")
    implementation("org.ow2.asm:asm:9.5")
    implementation("org.ow2.asm:asm-tree:9.5")
    runtimeOnly("ch.qos.logback:logback-classic:1.2.10")
}

tasks.named<JavaExec>("run") {
    val testResourceDir = file("../../test_resource")
    val goldenDir = file("golden")
    val strategyFilter = project.findProperty("strategy")?.toString() ?: "all"
    val sampleFilter = project.findProperty("sample")?.toString() ?: "all"
    args = listOf(testResourceDir.absolutePath, goldenDir.absolutePath, strategyFilter, sampleFilter)
}
