package net.gorry.mxv2;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * ファイルマネージャなどから MDX を渡されたとき (ACTION_VIEW) の窓口。
 *
 * ネイティブ側（src/openintent.cpp）から JNI で呼ばれる。Windows の
 * 「2 つめの mxv2 からパスを渡される」（src/singleinstance.cpp）に当たるもので、
 * 開く手順はどちらも main.cpp の OpenHandedPath へ合流する。
 *
 * **渡されるのは URI**（たいていは `content://...`）で、素のパスではない。
 * Android では素のパスで端末のファイルを触れない（SafBridge の説明）ので、
 * ref へ直す道は 2 つある:
 *
 *   1. **すでに許可のあるツリー (SAF) の中にあるなら、そこの ref にする。**
 *      ドキュメント ID の前方一致で調べる（`primary:Music` と
 *      `primary:Music/mdx/foo.mdx`）。この道なら PDX も同じフォルダから
 *      読めるし、ファイラーもそのフォルダを開く。
 *   2. それ以外は**どちらにするかをユーザーに尋ねる**（mxv2 側のダイアログ）。
 *      **フォルダの許可を取る**なら SAF のピッカーへ（parentDocUri をピッカーの
 *      初期位置に使う）。**このまま演奏する**なら**アプリのフォルダへ写す**
 *      （copyToDir）。単独のドキュメントの許可では隣の PDX を読めないので、
 *      写した場合は **PDX が付いてこない**。
 *
 * 呼ばれるスレッド: setActivity と onNewIntent は UI スレッド、
 * それ以外はネイティブのメインスレッド。状態は static にまとめて
 * synchronized で守る（SafBridge と同じ作り）。
 */
public class OpenIntentBridge {
	private static final String TAG = "mxv2";

	/** 写すファイルの上限。MDX は数十 KB なので、これを超えるものは相手が違う。 */
	private static final long MAX_COPY_BYTES = 64L * 1024 * 1024;

	private static Activity sActivity;

	/** 起動後に渡されたもの（onNewIntent）。ネイティブが poll() で汲む。 */
	private static final ArrayList<String> sPending = new ArrayList<String>();

	public static void setActivity(Activity a) {
		sActivity = a;
	}

	public static boolean available() {
		return sActivity != null;
	}

	// -------------------------------------------------------------------
	// 受け取り
	// -------------------------------------------------------------------

	/**
	 * mxv2 のコマンドライン引数。インテントの extra "args" が本体で、
	 * ACTION_VIEW で開かれたときは**その URI を最後に足す**
	 * （mxv2 の引数は「オプション…　最後に開くもの 1 つ」）。
	 */
	public static String[] argumentsFor(Intent intent) {
		final ArrayList<String> out = new ArrayList<String>();
		if (intent != null) {
			String[] args = intent.getStringArrayExtra("args");
			if (args != null) {
				for (int i = 0; i < args.length; i++) {
					if (args[i] != null) out.add(args[i]);
				}
			}
			final String uri = uriOf(intent);
			if (uri != null) out.add(uri);
		}
		return out.toArray(new String[out.size()]);
	}

	/**
	 * 動いている最中に渡されたとき（MainActivity.onNewIntent）。
	 * ここでは覚えるだけで、開くのはネイティブのメインループ。
	 */
	public static void onNewIntent(Intent intent) {
		final String uri = uriOf(intent);
		if (uri == null) return;
		synchronized (OpenIntentBridge.class) {
			// 続けざまに渡されても取りこぼさないよう、ためておく。
			sPending.add(uri);
		}
	}

	/** ためてあるものを 1 つ取り出す。無ければ null。 */
	public static synchronized String poll() {
		if (sPending.isEmpty()) return null;
		return sPending.remove(0);
	}

	/** ACTION_VIEW で渡された URI。そうでなければ null。 */
	private static String uriOf(Intent intent) {
		if (intent == null) return null;
		if (!Intent.ACTION_VIEW.equals(intent.getAction())) return null;
		final Uri uri = intent.getData();
		return (uri == null) ? null : uri.toString();
	}

	// -------------------------------------------------------------------
	// ref へ直す
	// -------------------------------------------------------------------

	/**
	 * 許可のあるツリーの中にあるなら "&lt;ツリーの URI&gt;\n&lt;相対パス&gt;" を返す。
	 * 見つからなければ null。
	 *
	 * 照らし合わせ方は 2 通りあり、上から順に試す。
	 *
	 *   1. **ドキュメント ID の前方一致**（`primary:Download/mdx` と
	 *      `primary:Download/mdx/ArctanX/am_field.mdx`）。渡されたものが
	 *      ツリーと同じ提供元のドキュメント URI のときだけ使える。
	 *   2. **実パスの前方一致**。ファイルマネージャによっては ExternalStorage
	 *      Provider ではなく **MediaStore や自前の FileProvider** の URI を
	 *      渡してくる。提供元が違うと 1. は空振りするので、どちらも
	 *      `/storage/emulated/0/...` まで落として比べる（2026-09-18、
	 *      「許可を出したフォルダなのにダイアログが出る」の報告で追加）。
	 *
	 * 相対パスは表示名を '/' で並べたものになる（SafBridge の流儀）。
	 * ドキュメント ID や実パスの綴りが表示名と食い違う提供元では合わないので、
	 * 合っているかはネイティブ側が存在確認で見る（外れたら尋ねる道へ回る）。
	 */
	public static String resolveInTree(String uriStr) {
		final Activity a = sActivity;
		if (a == null || uriStr == null) return null;
		try {
			final Uri uri = Uri.parse(uriStr);
			final List<UriPermission> list = a.getContentResolver().getPersistedUriPermissions();

			// 1. ドキュメント ID で
			final String docId = documentIdOf(uri);
			if (docId != null) {
				for (int i = 0; i < list.size(); i++) {
					final UriPermission p = list.get(i);
					if (!p.isReadPermission()) continue;
					final Uri tree = p.getUri();
					if (!tree.getAuthority().equals(uri.getAuthority())) continue;
					final String treeId = treeDocumentIdOf(tree);
					if (treeId == null) continue;
					final String rel = relativeDocId(treeId, docId);
					if (rel == null) continue;
					return tree.toString() + "\n" + rel;
				}
			}

			// 2. 実パスで（提供元が違っても、同じフォルダなら同じパスになる）
			final String path = realPath(uri);
			if (path != null) {
				for (int i = 0; i < list.size(); i++) {
					final UriPermission p = list.get(i);
					if (!p.isReadPermission()) continue;
					final Uri tree = p.getUri();
					final String treePath = pathFromDocId(treeDocumentIdOf(tree));
					if (treePath == null) continue;
					final String rel = relativePath(treePath, path);
					if (rel == null) continue;
					return tree.toString() + "\n" + rel;
				}
			}
		} catch (Exception e) {
			Log.w(TAG, "resolveInTree failed: " + uriStr, e);
		}
		return null;
	}

	/** ドキュメント URI ならそのドキュメント ID。そうでなければ null。 */
	private static String documentIdOf(Uri uri) {
		try {
			if (!ContentResolver.SCHEME_CONTENT.equals(uri.getScheme())) return null;
			if (!DocumentsContract.isDocumentUri(sActivity, uri)) return null;
			return DocumentsContract.getDocumentId(uri);
		} catch (Exception e) {
			return null;
		}
	}

	/** ツリー URI ならその根のドキュメント ID。そうでなければ null。 */
	private static String treeDocumentIdOf(Uri tree) {
		try {
			return DocumentsContract.getTreeDocumentId(tree);
		} catch (Exception e) {
			return null;  // 単独ドキュメントの許可など
		}
	}

	/**
	 * URI の指す実ファイルのパス（取れなければ null）。
	 *
	 * ドキュメント ID からの組み立て → `_data` 列 → 相対パス + 表示名
	 * (Android 10 以降の MediaStore は `_data` を伏せることがある) の順に試す。
	 */
	private static String realPath(Uri uri) {
		if (ContentResolver.SCHEME_FILE.equals(uri.getScheme())) return uri.getPath();
		if (!ContentResolver.SCHEME_CONTENT.equals(uri.getScheme())) return null;

		final String fromId = pathFromDocId(documentIdOf(uri));
		if (fromId != null) return fromId;

		Cursor c = null;
		try {
			c = sActivity.getContentResolver().query(uri, null, null, null, null);
			if (c != null && c.moveToFirst()) {
				final String data = columnString(c, "_data");
				if (data != null && data.startsWith("/")) return data;
				final String rel = columnString(c, "relative_path");
				final String name = columnString(c, "_display_name");
				if (rel != null && name != null) {
					final String root = volumeRoot(columnString(c, "volume_name"));
					final String dir = trimSlash(rel);
					return dir.isEmpty() ? (root + "/" + name) : (root + "/" + dir + "/" + name);
				}
			}
		} catch (Exception e) {
			// 許可の無い URI では query で撥ねられる。よくあることなので静かに。
			Log.d(TAG, "realPath not available: " + e);
		} finally {
			if (c != null) c.close();
		}
		return pathInUri(uri);
	}

	/**
	 * 最後の保険。**URI のパスに実パスがそのまま埋まっている**提供元がある
	 * （File Manager+ の
	 * `content://…fileprovider/root/storage/emulated/0/Download/mdx/foo.mdx` など）ので、
	 * "/storage/" から後ろを実パスとみなす。当てずっぽうだが、外れても
	 * ネイティブ側の存在確認 (`Vfs::Exists`) で弾かれて尋ねる道へ回るだけ。
	 */
	private static String pathInUri(Uri uri) {
		final String path = uri.getPath();
		if (path == null) return null;
		final int at = path.indexOf("/storage/");
		return (at < 0) ? null : path.substring(at);
	}

	/**
	 * ドキュメント ID からの実パス。"primary:Download/mdx" のように
	 * **ボリューム名 + ':' + 相対パス**の形のものだけ作れる
	 * （"msf:123" のような通し番号の ID では作れないが、そのときは
	 * どのツリーのパスにも前方一致しないので取り違えは起きない）。
	 */
	private static String pathFromDocId(String docId) {
		if (docId == null) return null;
		if (docId.startsWith("raw:")) return docId.substring(4);
		final int colon = docId.indexOf(':');
		if (colon <= 0) return null;
		final String root = volumeRoot(docId.substring(0, colon));
		final String rel = trimSlash(docId.substring(colon + 1));
		return rel.isEmpty() ? root : (root + "/" + rel);
	}

	/** ボリューム名から根のパス。"primary" と MediaStore の名前を同じに扱う。 */
	private static String volumeRoot(String volume) {
		if (volume == null || volume.isEmpty() || "primary".equalsIgnoreCase(volume) ||
		    "external".equalsIgnoreCase(volume) || "external_primary".equalsIgnoreCase(volume)) {
			return Environment.getExternalStorageDirectory().getAbsolutePath();
		}
		// SD カードなどは "XXXX-XXXX"。MediaStore は小文字で名乗るので直す。
		return "/storage/" + volume.toUpperCase(java.util.Locale.US);
	}

	private static String columnString(Cursor c, String name) {
		final int i = c.getColumnIndex(name);
		if (i < 0 || c.isNull(i)) return null;
		final String s = c.getString(i);
		return (s == null || s.isEmpty()) ? null : s;
	}

	/** 実パスの前方一致。root の外なら null。 */
	private static String relativePath(String root, String path) {
		if (root == null || path == null) return null;
		final String r = trimTrailingSlash(root);
		if (!path.startsWith(r)) return null;
		if (path.length() == r.length()) return "";
		if (path.charAt(r.length()) != '/') return null;  // "/Download/mdx2" の取り違えを防ぐ
		return trimSlash(path.substring(r.length() + 1));
	}

	private static String trimTrailingSlash(String s) {
		int e = s.length();
		while (e > 1 && s.charAt(e - 1) == '/') e--;
		return s.substring(0, e);
	}

	/**
	 * ツリーのドキュメント ID から見た相対。子孫でなければ null。
	 * ID は "primary:Music/mdx/foo.mdx" のような形で、根は "primary:" で終わる
	 * ことも "primary:Music" のように途中で終わることもある。
	 */
	private static String relativeDocId(String treeId, String docId) {
		if (treeId == null || docId == null) return null;
		if (treeId.equals(docId)) return "";
		if (!docId.startsWith(treeId)) return null;
		final String rest = docId.substring(treeId.length());
		if (treeId.endsWith(":") || treeId.endsWith("/")) return trimSlash(rest);
		if (rest.startsWith("/")) return trimSlash(rest.substring(1));
		return null;  // "primary:Music" と "primary:Music2/..." の取り違えを防ぐ
	}

	private static String trimSlash(String s) {
		int b = 0;
		int e = s.length();
		while (b < e && s.charAt(b) == '/') b++;
		while (e > b && s.charAt(e - 1) == '/') e--;
		return s.substring(b, e);
	}

	/**
	 * SAF のピッカーに最初に見せる場所（渡されたファイルの**親フォルダ**の
	 * ドキュメント URI）。作れなければ null（ピッカーは既定の場所から開く）。
	 *
	 * ドキュメント ID の末尾を落として作る。"primary:Music/foo.mdx" なら
	 * "primary:Music"、"primary:foo.mdx" のように '/' が無ければボリュームの
	 * 根 "primary:" にする。ID の作りは提供元しだいなので、当てが外れても
	 * ピッカーが黙って既定の場所を出すだけで済む。
	 */
	public static String parentDocUri(String uriStr) {
		final Activity a = sActivity;
		if (a == null || uriStr == null) return null;
		try {
			final Uri uri = Uri.parse(uriStr);

			// ドキュメント URI なら、そのドキュメント ID の末尾を落とすだけ。
			final String docId = documentIdOf(uri);
			if (docId != null) {
				final String parentId = parentDocId(docId);
				if (parentId != null) {
					return DocumentsContract.buildDocumentUri(uri.getAuthority(), parentId)
					    .toString();
				}
			}

			// そうでない提供元（自前の FileProvider や MediaStore）は、実パスから
			// **端末のストレージ (ExternalStorageProvider) のドキュメント URI**を
			// 組み立てる。ピッカーはそこを開くので、渡されたファイルのフォルダに
			// 立った状態で「このフォルダを使用」を押せる。
			final String path = realPath(uri);
			if (path == null) return null;
			final int slash = path.lastIndexOf('/');
			if (slash <= 0) return null;
			final String parentId = externalDocId(path.substring(0, slash));
			if (parentId == null) return null;
			return DocumentsContract
			    .buildDocumentUri("com.android.externalstorage.documents", parentId)
			    .toString();
		} catch (Exception e) {
			Log.w(TAG, "parentDocUri failed: " + uriStr, e);
			return null;
		}
	}

	/**
	 * ドキュメント ID の 1 つ上。"primary:Music/foo.mdx" なら "primary:Music"、
	 * '/' が無ければボリュームの根 "primary:"。作れなければ null。
	 */
	private static String parentDocId(String docId) {
		final int slash = docId.lastIndexOf('/');
		if (slash > 0) return docId.substring(0, slash);
		final int colon = docId.indexOf(':');
		if (colon < 0) return null;
		final String parentId = docId.substring(0, colon + 1);
		return parentId.isEmpty() ? null : parentId;
	}

	/**
	 * 実パス -> 端末のストレージのドキュメント ID
	 * （"/storage/emulated/0/Download/mdx" なら "primary:Download/mdx"）。
	 * そのボリュームの下でなければ null。
	 */
	private static String externalDocId(String path) {
		final String primary =
		    trimTrailingSlash(Environment.getExternalStorageDirectory().getAbsolutePath());
		if (path.equals(primary)) return "primary:";
		if (path.startsWith(primary + "/")) {
			return "primary:" + path.substring(primary.length() + 1);
		}
		// SD カードなど ("/storage/XXXX-XXXX/...")。
		final String prefix = "/storage/";
		if (!path.startsWith(prefix)) return null;
		final String rest = path.substring(prefix.length());
		final int slash = rest.indexOf('/');
		final String volume = (slash < 0) ? rest : rest.substring(0, slash);
		if (volume.isEmpty() || volume.equals("emulated") || volume.equals("self")) return null;
		return (slash < 0) ? (volume + ":") : (volume + ":" + rest.substring(slash + 1));
	}

	/** 画面に出す名前（提供元が名乗る表示名。取れなければ URI の末尾から）。 */
	public static String displayName(String uriStr) {
		if (sActivity == null || uriStr == null) return null;
		try {
			return displayName(Uri.parse(uriStr));
		} catch (Exception e) {
			Log.w(TAG, "displayName failed: " + uriStr, e);
			return null;
		}
	}

	/**
	 * dirPath へ写す。書けたファイルの名前を返す（書けなければ null）。
	 * 同じ名前のものは上書きする。
	 */
	public static String copyToDir(String uriStr, String dirPath) {
		final Activity a = sActivity;
		if (a == null || uriStr == null || dirPath == null) return null;

		InputStream in = null;
		OutputStream out = null;
		File dest = null;
		try {
			final Uri uri = Uri.parse(uriStr);
			final String name = displayName(uri);
			final File dir = new File(dirPath);
			if (!dir.isDirectory() && !dir.mkdirs()) {
				Log.e(TAG, "cannot create the folder: " + dirPath);
				return null;
			}
			dest = new File(dir, name);

			in = a.getContentResolver().openInputStream(uri);
			if (in == null) return null;
			out = new FileOutputStream(dest);
			final byte[] buf = new byte[64 * 1024];
			long total = 0;
			for (;;) {
				final int n = in.read(buf);
				if (n < 0) break;
				total += n;
				if (total > MAX_COPY_BYTES) {
					Log.e(TAG, "too large to copy: " + uriStr);
					out.close();
					out = null;
					dest.delete();
					return null;
				}
				out.write(buf, 0, n);
			}
			out.close();
			out = null;
			return name;
		} catch (Exception e) {
			Log.e(TAG, "copyToDir failed: " + uriStr, e);
			if (out != null) {
				try { out.close(); } catch (Exception ignored) { }
				out = null;
			}
			if (dest != null) dest.delete();
			return null;
		} finally {
			if (in != null) {
				try { in.close(); } catch (Exception ignored) { }
			}
			if (out != null) {
				try { out.close(); } catch (Exception ignored) { }
			}
		}
	}

	/**
	 * 写すときのファイル名。提供元が名乗る表示名が本命で、無ければ URI の
	 * 末尾から作る。フォルダの区切りとドライブの ':' は落とす
	 * （"primary:Music/foo.mdx" のような ID がそのまま来ることがある）。
	 */
	private static String displayName(Uri uri) {
		String name = null;
		if (ContentResolver.SCHEME_CONTENT.equals(uri.getScheme())) {
			Cursor c = null;
			try {
				c = sActivity.getContentResolver().query(
				    uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null);
				if (c != null && c.moveToFirst() && !c.isNull(0)) name = c.getString(0);
			} catch (Exception e) {
				// 許可の無い URI では query で撥ねられる（渡されたものの
				// 名前を出すだけなので、URI の末尾で代用する）。よくあること
				// なので、スタックトレースは出さない。
				Log.d(TAG, "DISPLAY_NAME not available: " + e);
			} finally {
				if (c != null) c.close();
			}
		}
		if (name == null) name = uri.getLastPathSegment();
		if (name == null) name = "";
		int cut = -1;
		for (int i = 0; i < name.length(); i++) {
			final char ch = name.charAt(i);
			if (ch == '/' || ch == '\\' || ch == ':') cut = i;
		}
		name = name.substring(cut + 1).trim();
		if (name.isEmpty()) name = "handed.mdx";
		return name;
	}
}
