package net.gorry.mxv2;

import org.libsdl.app.SDLActivity;

/**
 * mxv2 の Activity。
 *
 * SDLActivity は getLibraries() の**最後の名前**の共有ライブラリを dlopen し、
 * その中の SDL_main を呼ぶ。既定は "main"（libmain.so）だが、CMake の
 * ターゲット名を Windows と揃えたいので "mxv2"（libmxv2.so）にしている。
 */
public class MainActivity extends SDLActivity {
	/**
	 * mxv2 のコマンドライン引数。端末には「コマンドライン」が無いので、
	 * インテントの extra "args" から受け取れるようにしてある。
	 *
	 *   adb shell am start -n net.gorry.mxv2/.MainActivity --esa args "-skin,Default"
	 *
	 * ふつうに起動したときは何も渡らない（引数無しで起動したのと同じ）。
	 */
	@Override
	protected String[] getArguments() {
		String[] args = getIntent().getStringArrayExtra("args");
		return args != null ? args : new String[0];
	}

	@Override
	protected String[] getLibraries() {
		return new String[] {
			"SDL2",
			"mxv2",
		};
	}
}
