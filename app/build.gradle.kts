import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Release signing key: kept outside the repo. MPE_SIGNING names a properties file with storeFile,
// storePassword, keyAlias, keyPassword; default ~/.android-keys/mpe-instruments.properties.
// Without one the release APK is built unsigned.
val signingProps = Properties().apply {
    val f = file(System.getenv("MPE_SIGNING") ?: "${System.getProperty("user.home")}/.android-keys/mpe-instruments.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

android {
    namespace = "com.patrickreynolds.mpeinstruments"
    compileSdk = 35
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.patrickreynolds.mpeinstruments"
        minSdk = 31          // Android 12: AMidi (29) and the Bluetooth permission model (31)
        targetSdk = 35
        // -Pmpe.versionCode / -Pmpe.versionName: for testing in-app updates with an older-numbered build.
        versionCode = (findProperty("mpe.versionCode") as String?)?.toInt() ?: 60
        versionName = (findProperty("mpe.versionName") as String?) ?: "0.34.1"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }   // phone, and the emulator
        externalNativeBuild { cmake { arguments += listOf("-DANDROID_STL=c++_shared") } }
    }
    signingConfigs {
        if (signingProps.isNotEmpty()) create("release") {
            storeFile = file(signingProps.getProperty("storeFile"))
            storePassword = signingProps.getProperty("storePassword")
            keyAlias = signingProps.getProperty("keyAlias")
            keyPassword = signingProps.getProperty("keyPassword")
        }
    }
    buildTypes {
        release {
            signingConfig = signingConfigs.findByName("release")
            isMinifyEnabled = true                         // Compose is large unshrunk
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" } }
    // The controller packages that come with the app (controllers/, built to WebAssembly): the same
    // .odoc files a computer MESH plays. Built by controllers/build.sh, which needs clang.
    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/controllers"))
    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/takes"))
    // odo's fonts (ui/fonts, shared with odo for computers) as res/font, and their licences as assets.
    sourceSets["main"].res.srcDir(layout.buildDirectory.dir("generated/fonts/res"))
    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/fonts/assets"))
    buildFeatures { prefab = true; compose = true; buildConfig = true }          // Oboe ships as a prefab package
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("com.google.oboe:oboe:1.10.0")
    implementation(platform("androidx.compose:compose-bom:2024.10.01"))
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material:material-icons-core")
}

val buildControllers by tasks.registering(Exec::class) {
    val src = rootProject.file("controllers")
    inputs.dir(src).withPropertyName("controllers")
    outputs.dir(src.resolve("build"))
    commandLine(src.resolve("build.sh").path)
}
// The takes in takes/ as built-in takes (Takes → BUILT IN), to assess them. For a store release:
// -Podo.builtinTakes=false leaves them out.
val builtinTakes = (findProperty("odo.builtinTakes") as String?)?.toBoolean() ?: true
val takeAssets by tasks.registering(Sync::class) {
    if (builtinTakes) from(rootProject.file("takes")) { include("*.odotake") }
    into(layout.buildDirectory.dir("generated/takes/takes"))
}
val controllerAssets by tasks.registering(Copy::class) {
    dependsOn(buildControllers)
    from(rootProject.file("controllers/build")) { include("*.odoc") }
    into(layout.buildDirectory.dir("generated/controllers/controllers"))
}
// One copy of the fonts, in ui/fonts: resource names must be lower case, without dashes.
val fontResources by tasks.registering(Sync::class) {
    from(rootProject.file("ui/fonts")) { include("*.ttf"); rename { it.lowercase().replace('-', '_') }; into("res/font") }
    from(rootProject.file("ui/fonts")) { include("OFL-*.txt"); into("assets/licenses") }
    into(layout.buildDirectory.dir("generated/fonts"))
}
tasks.named("preBuild") { dependsOn(controllerAssets, takeAssets, fontResources) }
