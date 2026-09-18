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

- **Game-supplied CSS and JavaScript** (web renderer only; the classic renderer
  ignores them). Four special variables, read on every refresh exactly the way
  `$BACKIMAGE` and `USEHTML` already are, so nothing new has to be learned and a
  game never has to know which renderer it is running under:

  | Variable | Meaning |
  | --- | --- |
  | `$USERCSS` | CSS written inline |
  | `$USERCSSFILE` | path to a `.css` file, relative to the game folder |
  | `$USERJS` | JavaScript written inline |
  | `$USERJSFILE` | path to a `.js` file, relative to the game folder |

  Each one is also an array, so several pieces can be stacked:

  ```qsp
  $USERCSSFILE[] = 'ui/theme.css'
  $USERCSSFILE[] = 'ui/cards.css'
  $USERCSS = '.card { border-radius: 8px }'
  $USERJSFILE = 'ui/hud.js'
  ```

  Files are fetched over the same virtual host as the rest of the game's assets,
  so relative URLs inside them resolve normally. CSS is declarative and always
  reflects the current values; the files load before the inline block and the
  inline block wins, so it can override them. JavaScript runs when it first
  appears and whenever its text or its file list changes — never once per
  refresh — and the inline block waits for the files, so it can call into them.

  `<script>` tags inside an HTML-mode description now execute as well, once the
  content is on screen rather than while it is still staged off screen.

- **A JavaScript bridge to the engine**, exposed as `window.qsp`. Every call is
  a round trip to the engine and returns a promise:

  ```js
  await qsp.getVar('$NAME')        // item 0, or qsp.getVar('$NAME', 3)
  await qsp.setVar('COUNT', 5)     // or qsp.setVar('$A', 'key', 'value')
  await qsp.addVar('$LOG', 'line') // appends, like $LOG[] = 'line'
  await qsp.getVarSize('$LOG')
  await qsp.indexOf('$A', 'key')   // index of a string key, -1 if absent
  await qsp.exec("GT 'room'")      // a line of QSP code
  await qsp.eval('$NAME & "!"')    // string expression, 4095 chars max
  await qsp.evalNum('COUNT + 1')
  await qsp.execLoc('hud_update')
  qsp.onRefresh(() => { ... })     // fires after each finished pane update
  qsp.pane                         // 'main' or 'vars'
  ```

  A numeric variable comes back as a JS number and everything else as a string;
  the `$` prefix decides how a write is typed, matching QSP's own rule. The two
  description panes are separate documents, so the scripts run in both — `qsp.pane`
  tells them apart.

  Writes go through generated QSP code, which the engine has no setter for.
  Names are validated against the engine's own delimiter set, values are escaped
  as QSP literals, and a value containing `<<` is rebuilt with `REPLACE` so it is
  never mistaken for an expression substitution. Calls arriving while the engine
  is busy are rejected rather than queued, the same guard `EXEC:` links use.
- **Development API for external editors.** `qspgui --dev` opens a loopback
  JSON-RPC socket (`--dev-port`, default 4747; `--dev-token` to require a
  handshake) that reloads a running game, executes code and reports state.
  Off unless the flag is given, and bound to the loopback interface only.
  See [DEVAPI.md](DEVAPI.md) for the protocol.
  - `reload` swaps the game world under a live session and keeps the variables.
    The engine cannot patch a single location — `qspLocs` is private to the
    library, and loading a world with `isNewGame = QSP_FALSE` is the `INCLUDE`
    path, which skips existing names instead of replacing them. So the reload
    snapshots the session with `QSPSaveGameAsData`, loads the new world, and
    restores the snapshot, which resolves the current location by name against
    it. `DEBUG = 1` is set first because `qspCheckGameStatus` only verifies the
    game CRC when `DEBUG` is zero — that is what lets a save from the old build
    load into the new one.
  - A save also carries the rendered description and the literal code of the
    current actions, which is why the screen is stale until the location is
    entered again. `reenter` chooses between `goto`, `gosub`, `code` and
    `none`; `goto` is the default and re-runs the location's side effects.
  - `snapshot` / `restore` keep named saves in memory, so a scene can be
    replayed without going through the disk or a menu.
  - Runtime errors are pushed to the editor with the engine's own location,
    action index and line numbers, before the error dialog blocks the player.
  - Commands that arrive while the engine is yielding to the event loop with
    game code still on the stack (`SLEEP`, input and message dialogs, menus, a
    forced refresh) are queued and run from idle. `QSPDev::EngineScope` in
    `callbacks_gui.cpp` marks those windows.
  - `qspgui/devserver.{h,cpp}` — new `QSPDevServer`, plus a small JSON reader
    and writer, since wxWidgets ships neither.
- **Quick save and quick load on F5 / F9**, as two new entries in the Game menu.
  The slot sits next to the game file as `<game>_quick.sav`, one per game, and is
  kept apart from the Ctrl-S quicksave so it can't take over the file the player
  chose themselves. F5 respects `NOSAVE` and reports that the game doesn't allow
  saving; F9 is not gated by it, since a slot can only exist if the game allowed
  saving when it was written.
  - Under the web renderer both keys arrive as synthetic events from the browser
    pane, which is also where the browser's own shortcuts are turned off - F5
    would otherwise reload the shell out from under the game.

- **Toast notifications** (`qspgui/toast.{h,cpp}` — new `QSPToast`), used by quick
  save and quick load for the kind of status a modal dialog has no business
  interrupting play for. A floating frame rather than a child window, because the
  browser pane would paint over anything put on top of it.

- `CHANGELOG.md`.

### Changed

- QSP code generation (`ToQspLiteral`, `IsValidVarName`, `ToQspIndex`,
  `BuildAssignment`) moved from a private block in `frame.cpp` to `QSPCode` in
  `comtools`. It was behind `QSPGUI_USE_WEBVIEW` and is now shared with the
  development API, so escaping and name validation have one implementation
  rather than one per caller.

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
