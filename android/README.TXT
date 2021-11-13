This directoy contains Android Studio project files for building the NORM
library for Android.

REQUIREMENTS:

Ninja is required in order to run the ant build for the java .jar file

Download and install ndk and cmake using the SDK manager within Android Studio. To do this:

    -> Tools -> SDK Manager -> System Settings -> Android SDK -> SDK Tools -> Select 'Show Package Details' at the bottom

The latest compatible versions are ndk version 20.0.5594570 and cmake version 3.10.2.4988404

* Important *
If you decide to install different version of the SDK Tools above, make sure to edit the path locations in the local.properties file

TO BUILD:

./gradlew build

The lib/build/ directory will contain the binary libraries built.

The "./gradlew clean" command can be used to delete the files built.

This directory may also be opened as an Android Studio project and built from
the IDE GUI.

This will evolve as we update NORM and associated projects to the newer Android
Studio tool chain.

USAGE:

Copy the .aar file into your ${project.rootDir}/app/libs/ folder

Within your app level build.gradle include the following two tasks before your dependencies section:

```task extractSo(type: Copy) {
    println 'Extracting *.so file(s)....'

    from zipTree("${project.rootDir}/app/libs/lib-release_-release.aar")
    into "${project.rootDir}/app/src/main/jniLibs"
    include "jni/**/*.so"

    eachFile {
        def segments = it.getRelativePath().getSegments() as List
        println segments
        it.setPath(segments.tail().join("/"))
        return it
    }
    includeEmptyDirs = false
}

task extractJar(type: Copy, dependsOn: extractSo) {
    println 'Extracting *.jar file(s)....'

    from zipTree("${project.rootDir}/app/libs/lib-release_-release.aar")
    into "${project.rootDir}/app/libs/"
    include "norm-*.jar"
}```


