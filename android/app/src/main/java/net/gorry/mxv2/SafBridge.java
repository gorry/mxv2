package net.gorry.mxv2;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

/**
 * SAF (Storage Access Framework) の窓口。
 *
 * ネイティブ側（src/safaccess.cpp）から JNI で呼ばれる。Android では素の
 * パスで端末のフォルダを触れないので、ユーザーに選んでもらったツリー
 * （ACTION_OPEN_DOCUMENT_TREE の結果）越しに読む。
 *
 * **場所の指し方**: ツリーの URI（`content://.../tree/...`）と、そこからの
 * '/' 区切りの相対パス（表示名を並べたもの）の 2 つで指す。SAF はパスでは
 * なくドキュメント ID で辿る作りなので、相対パス → ドキュメント ID の
 * 変換はここで行い、結果を覚えておく（同じフォルダを何度も辿らないため）。
 *
 * **呼ばれるスレッドはまちまち**（フォルダ読み・曲名読み・曲の読み込みは
 * 別スレッド）。状態は static でまとめ、触るところは synchronized にしてある。
 */
public class SafBridge {
	private static final String TAG = "mxv2";

	/** ACTION_OPEN_DOCUMENT_TREE の requestCode。 */
	public static final int REQUEST_OPEN_TREE = 0x5AF0;

	private static Activity sActivity;

	/** 選び終わった結果。URI か、取り消したときは空文字列。まだなら null。 */
	private static String sResult;

	/** 相対パス -> ドキュメント ID。キーは "<ツリー URI>\n<相対パス>"。 */
	private static final HashMap<String, String> sDocIds = new HashMap<String, String>();

	public static void setActivity(Activity a) {
		sActivity = a;
	}

	public static boolean available() {
		return sActivity != null;
	}

	// -------------------------------------------------------------------
	// フォルダを選んでもらう
	// -------------------------------------------------------------------

	/**
	 * 選択画面を開く。開けたら true。結果は takeResult() で拾う。
	 * initialTreeUri を渡すと、その場所を最初に見せる（許可を取り直すときに
	 * 同じフォルダを選びやすくする。Android 8 (API 26) 以降でだけ効く）。
	 */
	public static boolean pickTree(final String initialTreeUri) {
		final Activity a = sActivity;
		if (a == null) return false;
		synchronized (SafBridge.class) {
			sResult = null;
		}
		a.runOnUiThread(new Runnable() {
			public void run() {
				Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
				i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
				           Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
				if (initialTreeUri != null && initialTreeUri.length() > 0 &&
				    android.os.Build.VERSION.SDK_INT >= 26) {
					try {
						Uri tree = Uri.parse(initialTreeUri);
						Uri doc = DocumentsContract.buildDocumentUriUsingTree(
						    tree, DocumentsContract.getTreeDocumentId(tree));
						i.putExtra(DocumentsContract.EXTRA_INITIAL_URI, doc);
					} catch (Exception e) {
						Log.w(TAG, "EXTRA_INITIAL_URI ignored", e);
					}
				}
				try {
					a.startActivityForResult(i, REQUEST_OPEN_TREE);
				} catch (Exception e) {
					Log.e(TAG, "cannot open the document tree picker", e);
					synchronized (SafBridge.class) {
						sResult = "";
					}
				}
			}
		});
		return true;
	}

	/** MainActivity.onActivityResult から呼ぶ。 */
	public static void onActivityResult(int request, int result, Intent data) {
		if (request != REQUEST_OPEN_TREE) return;

		String picked = "";
		if (result == Activity.RESULT_OK && data != null && data.getData() != null) {
			Uri uri = data.getData();
			// 次の起動でも読めるように、権限を持ち越す。
			try {
				sActivity.getContentResolver().takePersistableUriPermission(
				    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
				picked = uri.toString();
			} catch (Exception e) {
				Log.e(TAG, "takePersistableUriPermission failed", e);
			}
		}
		synchronized (SafBridge.class) {
			sResult = picked;
		}
	}

	/**
	 * 選び終わっていれば結果を返す（URI、または取り消しの空文字列）。
	 * まだなら null。一度返したら忘れる。
	 */
	public static synchronized String takeResult() {
		String s = sResult;
		sResult = null;
		return s;
	}

	// -------------------------------------------------------------------
	// ファイルシステムとしての読み出し
	// -------------------------------------------------------------------

	/** その URI の権限がまだ生きているか（次の起動で確かめる）。 */
	public static boolean hasPermission(String treeUri) {
		final Activity a = sActivity;
		if (a == null || treeUri == null) return false;
		try {
			List<UriPermission> list = a.getContentResolver().getPersistedUriPermissions();
			for (int i = 0; i < list.size(); i++) {
				UriPermission p = list.get(i);
				if (p.isReadPermission() && treeUri.equals(p.getUri().toString())) return true;
			}
		} catch (Exception e) {
			Log.e(TAG, "getPersistedUriPermissions failed", e);
		}
		return false;
	}

	/** ツリーの根の表示名（一覧に出す名前）。取れなければ null。 */
	public static String rootName(String treeUri) {
		try {
			Uri tree = Uri.parse(treeUri);
			String id = DocumentsContract.getTreeDocumentId(tree);
			Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, id);
			Cursor c = resolver().query(
			    doc, new String[] { DocumentsContract.Document.COLUMN_DISPLAY_NAME },
			    null, null, null);
			if (c == null) return null;
			try {
				if (c.moveToFirst()) return c.getString(0);
			} finally {
				c.close();
			}
		} catch (Exception e) {
			Log.e(TAG, "rootName failed: " + treeUri, e);
		}
		return null;
	}

	/**
	 * フォルダの中身。1 項目 1 要素で "D<名前>"（フォルダ）か "F<名前>"。
	 * 読めなければ null（＝そこは開けない）。
	 */
	public static String[] list(String treeUri, String rel) {
		try {
			Uri tree = Uri.parse(treeUri);
			String docId = resolveDocId(tree, treeUri, rel);
			if (docId == null) return null;

			Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
			Cursor c = resolver().query(kids, new String[] {
			    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
			    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
			    DocumentsContract.Document.COLUMN_MIME_TYPE,
			}, null, null, null);
			if (c == null) return null;

			ArrayList<String> out = new ArrayList<String>();
			try {
				final String prefix = rel.isEmpty() ? "" : (rel + "/");
				while (c.moveToNext()) {
					String childId = c.getString(0);
					String name = c.getString(1);
					String mime = c.getString(2);
					if (name == null || name.isEmpty()) continue;
					// 名前に '/' が入っていると相対パスが壊れるので落とす。
					if (name.indexOf('/') >= 0) continue;
					boolean isDir = DocumentsContract.Document.MIME_TYPE_DIR.equals(mime);
					out.add((isDir ? "D" : "F") + name);
					// ここで覚えておくと、この中へ入るときに辿り直さずに済む。
					putDocId(treeUri, prefix + name, childId);
				}
			} finally {
				c.close();
			}
			return out.toArray(new String[out.size()]);
		} catch (Exception e) {
			Log.e(TAG, "list failed: " + rel, e);
			return null;
		}
	}

	/** ファイルの中身。読めなければ null。 */
	public static byte[] read(String treeUri, String rel) {
		InputStream in = null;
		try {
			Uri tree = Uri.parse(treeUri);
			String docId = resolveDocId(tree, treeUri, rel);
			if (docId == null) return null;
			Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, docId);
			in = resolver().openInputStream(doc);
			if (in == null) return null;

			ByteArrayOutputStream bos = new ByteArrayOutputStream(64 * 1024);
			byte[] buf = new byte[64 * 1024];
			for (;;) {
				int n = in.read(buf);
				if (n < 0) break;
				bos.write(buf, 0, n);
			}
			return bos.toByteArray();
		} catch (Exception e) {
			Log.e(TAG, "read failed: " + rel, e);
			return null;
		} finally {
			if (in != null) {
				try { in.close(); } catch (Exception ignored) { }
			}
		}
	}

	/** -1 = 無い / 0 = ファイル / 1 = フォルダ。 */
	public static int stat(String treeUri, String rel) {
		try {
			Uri tree = Uri.parse(treeUri);
			String docId = resolveDocId(tree, treeUri, rel);
			if (docId == null) return -1;
			Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, docId);
			Cursor c = resolver().query(
			    doc, new String[] { DocumentsContract.Document.COLUMN_MIME_TYPE },
			    null, null, null);
			if (c == null) return -1;
			try {
				if (!c.moveToFirst()) return -1;
				String mime = c.getString(0);
				return DocumentsContract.Document.MIME_TYPE_DIR.equals(mime) ? 1 : 0;
			} finally {
				c.close();
			}
		} catch (Exception e) {
			Log.e(TAG, "stat failed: " + rel, e);
			return -1;
		}
	}

	/** 覚えている対応づけを捨てる（ファイラーの再読込）。 */
	public static synchronized void forget(String treeUri) {
		if (treeUri == null) {
			sDocIds.clear();
			return;
		}
		final String prefix = treeUri + "\n";
		java.util.Iterator<String> it = sDocIds.keySet().iterator();
		while (it.hasNext()) {
			if (it.next().startsWith(prefix)) it.remove();
		}
	}

	// -------------------------------------------------------------------

	private static ContentResolver resolver() {
		return sActivity.getContentResolver();
	}

	private static synchronized String getDocId(String treeUri, String rel) {
		return sDocIds.get(treeUri + "\n" + rel);
	}

	private static synchronized void putDocId(String treeUri, String rel, String docId) {
		// 際限なく増えないよう、大きくなったら捨てて拾い直す。
		if (sDocIds.size() > 4000) sDocIds.clear();
		sDocIds.put(treeUri + "\n" + rel, docId);
	}

	/**
	 * 相対パス -> ドキュメント ID。根から 1 段ずつ表示名で探す。
	 * 途中の段も覚えるので、同じ枝を何度も辿らずに済む。
	 */
	private static String resolveDocId(Uri tree, String treeUri, String rel) {
		final String rootId = DocumentsContract.getTreeDocumentId(tree);
		if (rel == null || rel.isEmpty()) return rootId;

		String hit = getDocId(treeUri, rel);
		if (hit != null) return hit;

		String[] parts = rel.split("/");
		String docId = rootId;
		StringBuilder path = new StringBuilder();
		for (int i = 0; i < parts.length; i++) {
			if (parts[i].isEmpty()) continue;
			if (path.length() > 0) path.append('/');
			path.append(parts[i]);

			final String sofar = path.toString();
			String cached = getDocId(treeUri, sofar);
			if (cached != null) {
				docId = cached;
				continue;
			}
			docId = findChild(tree, docId, parts[i]);
			if (docId == null) return null;
			putDocId(treeUri, sofar, docId);
		}
		return docId;
	}

	private static String findChild(Uri tree, String parentId, String name) {
		Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
		Cursor c = resolver().query(kids, new String[] {
		    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
		    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
		}, null, null, null);
		if (c == null) return null;
		try {
			while (c.moveToNext()) {
				if (name.equals(c.getString(1))) return c.getString(0);
			}
		} finally {
			c.close();
		}
		return null;
	}
}
