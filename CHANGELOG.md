# Changelog

All notable changes to this fork are documented here.

This fork tracks [QSPFoundation/qspgui](https://github.com/QSPFoundation/qspgui) and
adds an optional renderer built on a real browser engine, so games can use modern
media formats and CSS. The classic renderer remains the default and is unchanged.

## [Unreleased]

### Added

- **Optional wxWebView renderer for the main and additional description panes.**
  Enabled with `-DQSPGUI_USE_WEBVIEW=ON`; the build defaults to `OFF`, which
  produces the classic player exactly as before. Backed by WebView2 on Windows,
  WebKitGTK on Linux and WKWebView on macOS.
  - Modern media in descriptions: WebP (including animated), WebM, MP4, APNG.
    Video renders as a real `<video>` element with native controls.
  - Real CSS: grid, flexbox, custom properties, `<span style="...">` and the rest.
  - `qspgui/webtextbox.{h,cpp}` — new `QSPWebTextBox`, API-compatible with
    `QSPTextBox`, selected through the `QSPMainTextBox` typedef in `frame.h`.
- `CHANGELOG.md`.

### Fixed

- **Flicker on every action in real games.** Games rebuild their whole description
  on each refresh through the usual `GS`/`GOSUB` chain, so the incoming HTML almost
  always differs somewhere — a clock, a counter, a stat bar — even when the visible
  media is identical. The renderer was assigning `innerHTML`, which destroys and
  recreates every node; a recreated `<img>` is re-fetched, re-decoded and re-laid-out,
  and the browser paints the gap before it finishes. Panes carrying icons or images
  therefore flashed on every single action.

  The classic renderer does not show this because `SetPage` runs inside
  `Freeze()`/`Thaw()` and wxHtmlWindow decodes images synchronously, so its repaint is
  atomic. A browser engine updates asynchronously, which makes the teardown visible.

  The shell now reconciles the existing DOM against the new markup and touches only
  what actually differs, so an element whose attributes are unchanged — an `<img>`
  with the same `src` above all — is never recreated. Note this is a DOM teardown, not
  a page reload: navigation is vetoed after the shell loads, and the document is only
  ever re-navigated when the game folder changes.

### Implementation notes

These are the details that make the port behave; they are easy to get wrong.

- **No flicker on location change.** The shell document is loaded once and never
  navigated again. Text updates replace a subtree via `innerHTML` on the live DOM,
  so the compositor has nothing to repaint from scratch. Any navigation after the
  shell is up is vetoed.
- **Scripts are always run asynchronously.** QSP callbacks fire from inside engine
  script execution, and the synchronous `RunScript` pumps a nested message loop,
  which re-enters the engine.
- **Theming is shared with the rest of the UI.** Background, foreground, link colour
  and font are pushed into the document as CSS custom properties from the same
  settings the classic panes use, so the webview cannot drift to a different colour.
  The browser's pre-paint colour is set as well, so neither startup nor resize
  flashes white.
- **Game assets are served over a virtual host** (`qsp.game`) rather than `file://`.
  This gives media a real origin, which `<video>` seeking needs, and preserves the
  containment `ComposeGamePath` enforced, because the URL parser collapses `..` and
  cannot escape the mapped folder. The shell is served from its own host
  (`qsp.shell`) because a `file://` document is not permitted to load subresources
  from a virtual host at all.
- **A host mapping only applies to documents loaded after it is registered.**
  Registering one against a live document does not fail — its requests hang, because
  the name falls through to real DNS resolution. The shell is therefore reloaded when
  the game folder changes, which happens once per game load, not per location.
- **Keyboard events are forwarded back to the frame.** Keys pressed inside a browser
  control never reach wxWidgets, which would otherwise silently disable the 1-9
  action hotkeys, Space, and Escape-to-exit-fullscreen.
- **Links report their raw `href` attribute**, not the browser's resolved URL, so
  `#anchor` and `EXEC:` keep their existing meaning and `QSPFrame::OnLinkClicked`
  is used unmodified.

### Not yet ported

- The action and object lists (`QSPListBox`) and the image pane (`QSPImgCanvas`)
  still use the classic renderer.
- `msgdlg` / `inputdlg` still use `QSPTextBox`; they rely on
  `GetInternalRepresentation()` for auto-sizing.
- On GTK and macOS the document base falls back to `file://`, which does not provide
  the containment property described above.
