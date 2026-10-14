# webview (vendored)

`webview.h` is the single-header amalgamation of
[webview/webview](https://github.com/webview/webview) release **0.12.0**
(commit 3ab4b5d722438fc8a13e6ca830c5e2372d19a01d), MIT licence (see `LICENSE`).

It is used unmodified by `chains-app` (src/webui). On Linux it uses WebKitGTK 4.1
with GTK 3, on Windows WebView2, on macOS WKWebView.

To update: replace `webview.h` with `core/include/webview/webview.h` of the new
release, and update the version above.
