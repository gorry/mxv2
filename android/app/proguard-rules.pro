# SDL の Java は JNI から名前で呼ばれるので、縮小するときも消さないこと。
-keep class org.libsdl.app.** { *; }
