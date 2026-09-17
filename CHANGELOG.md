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

- **Flashing on every action in real games.** `QSPFrame::ShowPane` froze and thawed
  the whole frame on entry, before deciding whether anything needed to change —
  and games call it constantly through `SHOWSTAT` / `SHOWACTS` / `SHOWOBJS`, nearly
  always with the pane already in the state being asked for. A trace of one MAESTAT
  session recorded 283 of these cycles, arriving in bursts of four to six
  immediately before every content update, none of which changed the layout.

  `wxWindowBase::Freeze()` recurses into every child, so each cycle sent
  `WM_SETREDRAW FALSE`/`TRUE` plus a refresh to the browser control. Ordinary
  controls satisfy that refresh synchronously, which is why the action and status
  panes never showed it; a browser re-composites on its own schedule, so it went
  blank and filled in a frame or more later. Freezing now wraps only an actual
  relayout.

- **Blanking while a pane was being rebuilt.** Anything that replaces the document,
  or the content of the live one, is shown *while it is still being built*.
  A navigation blanks the view before the new page paints. Assigning `innerHTML` tears the old tree
  down first, and a freshly created `<img>` is re-fetched, re-decoded and
  re-laid-out with the browser painting the gap in between. The classic renderer
  has neither problem: `wxHtmlWindow` decodes synchronously inside
  `Freeze()`/`Thaw()`, so it steps from one finished state straight to the next
  and the intermediate state never exists.

  The renderer now does the same thing explicitly. The document holds two stacked
  layers that differ only in which is visible. An update fills the *hidden* layer,
  waits until it is laid out and its images and videos have loaded, and only then
  swaps which layer is visible, inside a single animation frame. The old content
  stays on screen the whole time, so there is no blank frame and no half-decoded
  image. A slow or missing asset cannot strand the pane on stale content: the swap
  happens anyway after 400 ms.

### Implementation notes

These are the details that make the port behave; they are easy to get wrong.

- **The document is never navigated after startup.** A navigation blanks the view
  first, which is the flash this renderer exists to avoid, so any navigation once
  the shell is up is vetoed and links are routed through the script message
  channel. The single exception is a game folder change — once per game load,
  never per location.
- **Updates are coalesced per refresh.** A refresh sets text, colours, font and
  background separately, and the engine often clears a pane and refills it in the
  same pass. `BeginUpdate`/`EndUpdate` around the refresh, plus a deferred flush,
  collapse all of that into one staged update.
- **The shell URL carries a hash of the shell.** It is fetched through the
  browser's cache, so without this a build that changes the shell would keep being
  served the previous version — a document with no update function in it, i.e. a
  permanently blank pane.
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
