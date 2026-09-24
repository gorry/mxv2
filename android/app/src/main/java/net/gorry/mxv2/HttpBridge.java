package net.gorry.mxv2;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * HTTPS で 1 つの URL を取ってくる窓口（更新チェック用）。
 *
 * ネイティブ側（src/httpget.cpp）から JNI で、**作業スレッドから**呼ばれる。
 * Android はメインスレッドでの通信を禁じているが、呼び出し元がもともと
 * 別スレッドなので、ここでは素直に待ってよい。
 *
 * 依存を増やさないため、枠組みの HttpURLConnection だけで書いてある。
 */
public final class HttpBridge {
	private HttpBridge() {}

	/** 本文をここで打ち切る（src/httpget.h の kMaxBodyBytes と同じ）。 */
	private static final int MAX_BODY_BYTES = 1024 * 1024;

	/**
	 * url を GET する。
	 *
	 * @return { 状態番号, 本文 (UTF-8), 誤り }。通信できなかったときは
	 *         状態番号が "0" で、誤りに理由が入る（成功なら誤りは空）。
	 *         200 以外の応答も本文を返す（誤りは空のまま）。
	 */
	public static String[] get(String url, String userAgent, String accept, int timeoutMs) {
		HttpURLConnection conn = null;
		try {
			conn = (HttpURLConnection) new URL(url).openConnection();
			conn.setConnectTimeout(timeoutMs);
			conn.setReadTimeout(timeoutMs);
			conn.setInstanceFollowRedirects(true);
			conn.setRequestProperty("User-Agent", userAgent);
			if (accept != null && !accept.isEmpty()) {
				conn.setRequestProperty("Accept", accept);
			}
			final int code = conn.getResponseCode();
			// 200 番台以外は getInputStream が例外を投げるので、誤りの本文を読む。
			InputStream in = (code >= 200 && code < 300) ? conn.getInputStream()
			                                             : conn.getErrorStream();
			String body = "";
			if (in != null) {
				try {
					ByteArrayOutputStream out = new ByteArrayOutputStream();
					byte[] buf = new byte[8192];
					int n;
					while ((n = in.read(buf)) > 0) {
						out.write(buf, 0, n);
						if (out.size() > MAX_BODY_BYTES) {
							return new String[] { String.valueOf(code), "", "response too large" };
						}
					}
					body = out.toString("UTF-8");
				} finally {
					in.close();
				}
			}
			return new String[] { String.valueOf(code), body, "" };
		} catch (Exception e) {
			return new String[] { "0", "", e.toString() };
		} finally {
			if (conn != null) conn.disconnect();
		}
	}
}
