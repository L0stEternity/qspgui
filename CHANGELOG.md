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

  For games coming from qQSP, a `custom.css` and `custom.js` next to the world
  file are loaded automatically, as qQSP does, without being named in any
  variable. They go into the two description panes only (the only HTML qQSP put
  them in) and ahead of anything in `$USERCSSFILE` / `$USERJSFILE`, so the
  game's own files still win. The folder is checked when the game is opened.

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
- **A profiler and a live performance monitor in the development API**, so
  "the game feels slow" becomes a line number. See [DEVAPI.md](DEVAPI.md).
  - `profile` reuses the engine's per-line debug callback for timing rather
    than for tracing: the interval between two calls is the time the engine
    spent on the line the first one reported, which makes it an exact
    measurement per line rather than a statistical sample. It reports self,
    inclusive and wait time per location, per-line hits with the worst single
    execution, and the call graph as caller/callee edges — a flame graph, a
    heat map over the source and a hot-lines table all come out of one report.
  - The call graph is inferred. `QSPGetCurStateData` reports where the engine
    is, never how it got there, so a line from a location already on the
    inferred stack is treated as a return to it and anything else as a call.
    Exact for the `GOTO` and `GOSUB` chains a game is made of; a location that
    recurses into itself is counted as one frame rather than as nested ones.
  - Time spent waiting on the player — `MSG`, `INPUT`, `SLEEP`, a held
    breakpoint — is reported separately from time spent interpreting, because
    a game is not slow for having asked a question.
  - The player times its own work too (`qspgui/devprofile.{h,cpp}`), which is
    the half a line profiler cannot see: each refresh, each description and
    list pane rebuilt, each script handed to a browser pane, each image, sound,
    dialog and save, with the number of bytes moved where that is what explains
    the cost. The counters are compile-time ids in a fixed table and cost a
    branch while a profile is running, nothing otherwise.
  - `monitor` pushes a `perf` sample on a timer — lines per second, the share
    of the window spent inside the interpreter, resident memory, and the
    counters for that window — so an editor can graph a session live. It runs
    off a timer rather than the idle handler, so samples keep arriving while
    the game sits still, and it does not install the per-line hook, so it is
    cheap enough to leave on for a whole session.
  - Both are answered while the game is held at a breakpoint, reading counters
    being something that cannot call into the interpreter. Starting or stopping
    the profiler there is refused rather than installing the debug hook from
    inside the debug hook.
  - `QSPJsonBuilder` grew `MemberDouble` and `MemberInt64`. Timings are written
    with the decimal point JSON requires whatever the C locale has to say about
    it — under a European locale `0,010` is two values, not one — and hit
    counts are 64-bit, a single runaway loop being enough to pass 2^31.
- **Quick save and quick load on F5 / F9**, as two new entries in the Game menu.
  The slot sits next to the game file as `<game>_quick.sav`, one per game, and is
  kept apart from the Ctrl-S quicksave so it can't take over the file the player
  chose themselves. F5 respects `NOSAVE` and reports that the game doesn't allow
  saving; F9 is not gated by it, since a slot can only exist if the game allowed
  saving when it was written.
  - Under the web renderer both keys arrive as synthetic events from the browser
    pane, which is also where the browser's own shortcuts are turned off - F5
    would otherwise reload the shell out from under the game.

- **Status messages** (`qspgui/toast.{h,cpp}` — new `QSPToast`), used by quick
  save and quick load for the kind of status a modal dialog has no business
  interrupting play for. A tooltip-sized panel with a chiselled border and a
  hand-drawn symbol, in the bottom right corner of the window, painted in the
  theme's colours — no rounded corners, no accent bar and no fading, which is
  not what this player looks like. A floating frame rather than a child window,
  because the browser pane would paint over anything put on top of it.

- **The action list, the object list and the image pane render in the browser
  engine too** (`qspgui/weblistbox.{h,cpp}` — new `QSPWebListBox`;
  `qspgui/webimgcanvas.{h,cpp}` — new `QSPWebImgCanvas`), selected through the
  `QSPMainListBox` and `QSPMainImgCanvas` typedefs alongside `QSPMainTextBox`.
  The whole player is now one renderer rather than two, so a game's CSS reaches
  its lists and its picture as well as its prose, and item images get the same
  modern formats the descriptions already had.
  - The image pane was the clearest win: the classic canvas decodes with
    `wxImage`, caches a scaled `wxBitmap` against the pane size, redoes that on
    every resize, and hands animated GIFs to a separate always-on-top child
    window because wxWidgets cannot animate inside a paint handler. That is one
    `<img>` and `object-fit: contain` here — and WebP, AVIF and APNG come free.
  - Behaviour is unchanged where readers would notice: the action list still
    selects on hover and runs on a single click, the object list still selects
    on click, `[1]`-style numbering still follows the hotkeys setting, and the
    selection still uses the desktop's own highlight colours rather than the
    game's.
  - Both lists share one document, and it is rebuilt only when the contents
    actually changed — games call `SHOWACTS` and `SHOWOBJS` constantly with the
    list already holding what is being set.

- **`qspgui/webpane.{h,cpp}` — new `QSPWebPane`**, the base every web-backed
  pane now derives from. It owns the view, the shell document, the virtual
  hosts, the navigation veto, keyboard forwarding and the developer aids;
  a subclass supplies its own document and is told when it is live. `QSPWebTextBox`
  was rebased onto it, so that machinery has one copy rather than four.

- **Numbered save slots** (`qspgui/saveslots.{h,cpp}` — new `QSPSaveSlots`),
  reached through **Game → Save slots... (F6)**. Nine slots per game, beside the
  game file the way the quick slot already is, so uninstalling a game takes its
  saves with it.
  - One dialog (`qspgui/saveslotsdlg.{h,cpp}` — new `QSPSaveSlotsDlg`) holds the
    lot: every slot in a list, with **Save**, **Load** and **Delete** acting on
    the selected row. 1 to 9 pick a slot from the keyboard, and double-click or
    Enter loads a used slot or saves into an empty one. Overwriting and deleting
    ask first; nothing else does.
  - The list shows what is in each slot — location and timestamp — because a
    `.sav` says nothing about itself: the engine's format has no header a player
    can read without loading it, and loading it is the one thing a "which save
    is this?" question must not do. A sidecar written next to the game carries
    the description.
  - The sidecar is advisory throughout. The save file is the truth: a slot whose
    `.sav` was deleted from the file manager is empty however the sidecar
    describes it, and a `.sav` dropped in by hand still loads, just with nothing
    to show for itself in the list. The list is read off the files every time it
    is shown, so a slot written by a second copy of the player reads correctly.
  - Deleting a slot removes the `.sav` first and its sidecar line second: a
    description without a save is harmless, a save nobody can describe is not.
  - Under the web renderer F6 arrives as a synthetic event from whichever pane
    has the focus, the way F5 and F9 already did — and now from all four of
    them rather than only the two description panes. Which pane the reader last
    clicked in is not something a save key should turn on.

- **Breakpoints and stepping in the development API.** `break`, `pause` and
  `resume`, with `paused` / `resumed` notifications. The engine has no debugger;
  what it has is a callback per executed line, which is enough to decide whether
  to stop — and stopping is simply not returning from it.
  - The pause does not pump the event loop. It reads the client sockets
    directly, and only the commands that cannot call back into the interpreter
    are answered while it is held — `resume`, `pause`, `break`, `ping`, `hello`
    and `getVar`. Everything else keeps its place in the queue and runs on the
    resume; it is late, not refused.
  - Pumping was the first attempt and it was unsound twice over, which is worth
    recording because it looks reasonable. A breakpoint is reached from inside
    `Dispatch` — an `exec` is what ran the code — so `m_inCommand` was already
    set and the nested drain could never serve anything: the pause could not be
    resumed at all. And a client dropped during the nested pump was destroyed
    while the outer `Dispatch` frame still held its pointer and was about to
    write the response to it, which segfaulted the player.
  - Losing every client resumes the game rather than leaving the player wedged
    on a breakpoint nobody is left to clear, and a client that disconnects while
    stopped is left alone until then rather than torn down mid-pause. The debug
    callback is installed only while tracing, a breakpoint or a pending step
    wants it, so an ordinary session pays nothing for it.

- **Errors in a game's own JavaScript are reported** instead of vanishing.
  Throws, rejected promises, `console.error` / `console.warn` and assets that
  would not load all reach the host, which logs them and pushes a `scriptError`
  notification to a connected editor. `$USERJS` is a supported feature, so a
  syntax error in a game's `hud.js` should not be indistinguishable from a
  script that does nothing.

- **Developer tools**, on under `--dev` and off otherwise: F12 opens them, and
  the browser context menu is left enabled as the way in on backends with no
  devtools API. Shipping `$USERCSS` and `$USERJS` without a way to debug them
  was half a feature.

- **`--log-file` and `--log-level`.** A GUI build has no console attached on
  Windows, so the stock stderr target drops everything it is given — which is
  why a bug report never arrives with a log. Dev mode turns the log on by
  itself, next to the executable, since an editor driving the player is exactly
  when the detail is wanted. The file is truncated past 4 MB rather than rotated.

- **A light / dark theme for the player's own frame**, as a radio group under
  Colors: "Follow system light / dark theme", "Light theme", "Dark theme". A
  fresh config follows the desktop, so the player matches everything else on
  screen, and either theme can be picked instead — which is what the first
  version of this was missing, since it only ever followed the desktop and had
  no way back.

  The theme is the frame and nothing else: the pane captions, the sashes, the
  border, the menus, the dropdowns, the scrollbars and the common dialogs. The
  page, the text and the links stay exactly where they were — they belong to the
  game's `$BCOLOR` / `$FCOLOR` / `$LCOLOR` and to the three colour settings above
  it, and the theme does not touch them.

  The captions follow the moment the theme is picked, because wxAUI draws those
  and we own its colours. The menus and the dialogs are Windows' own, and
  wxWidgets can only choose light or dark for them before the first window
  exists — so the setting is read straight out of the config file at startup
  (`QSPApp::ApplyStoredAppearance`), and a switch while the player is running
  says so in a toast rather than leaving it as a surprise.

- **Unit tests** (`tests/`), built with `-DQSPGUI_BUILD_TESTS=ON` and run through
  `ctest`. 51 cases over the pieces that are parsers or boundary checks, which is
  exactly the code that should not be verified by running the player and looking
  at it:
  - `QSPCode` — the escaping that turns outside input into a line of QSP the
    engine will run, including the `<<`-substitution defusal and the variable
    names that would otherwise become code.
  - `QSPJsonReader` / `QSPJsonBuilder` — a hand-written parser sitting on a
    socket, including malformed input and braces inside strings.
  - `QSPPaths` — the containment that keeps a game inside its own folder.
  - `QSPDockLayout` — the perspective rewrite that keeps each dock at the same
    share of the window.
  - `QSPFileIO` and `QSPSaveSlots`.
  - The runner is in `tests/testing.{h,cpp}`: small enough not to be a
    dependency, and a failing check records itself rather than aborting, so one
    run reports every broken expectation instead of only the first.

- **A one-step release build for Windows** (`build_release_msvc.ps1`). Configures,
  builds, installs, verifies and zips the player in a single command, producing
  one folder that runs from anywhere:

  ```
  .\build_release_msvc.ps1 -Version 5.9.6
  ```

  Previously a release had to be assembled by hand: `cmake --build` left the exe
  and `qsp.dll` in the build tree with no translations and no soundfont beside
  them, so the player fell back to English and silent MIDI unless a separate
  install step was run. The version defaults to `git describe`, the renderer to
  wxWebView (`-Classic` for the other one), and `-Tests` gates the package on the
  unit tests passing. The script is the native counterpart to
  `build_release_windows.sh`, which cross-compiles the official release in docker
  and is unchanged.

  Before zipping it checks the things that have actually been shipped broken: that
  the binary carries a version resource, that `qsp.dll`, `langs` and
  `sound/midi.sf2` are present, that there is one catalogue per `.po` file, and
  that no catalogue is older than its source. A stray `qspgui.cfg` left by a test
  run is dropped rather than shipped, since it carries window geometry and the
  last used language.

- **Translations are compiled from `create_lang/*.po` during the build.** They
  were generated by hand and committed, so editing a `.po` without remembering to
  re-run the converter left the built `.mo` behind — which is what had happened:
  the committed catalogues held 99 strings against the 106 in the sources, so the
  quick save and load messages appeared untranslated in every language.

  `build_packages/msgfmt.py` compiles them, since gettext is not present on a
  stock Windows box; a real `msgfmt` is used instead when one is installed, as it
  also validates the catalogue. With neither available the build falls back to the
  committed files and says so. Fuzzy and empty translations are skipped, as
  gettext does, so an unconfirmed guess shows the original string instead.

- **A version resource in `qspgui.exe`.** The binary reported no version at all to
  Explorer, to installers, or to anything else that reads file metadata.
  `APP_VERSION` now also produces `FILEVERSION` / `PRODUCTVERSION` and the
  accompanying strings; a pre-release suffix such as `5.9.6-b1` survives in the
  displayed version and is truncated to `5.9.6` for the numeric fields, which only
  take integers.

- `CHANGELOG.md`.

### Changed

- **A new application icon, drawn as pixel art.** The old mark was a red Q on
  an opaque white square, which put a white box on every dark taskbar and
  titlebar; it is now a Q on a dark rounded tile, with a tail that crosses the
  bowl so it still reads as a Q at sixteen pixels.

  The source is `misc/common/icons/qspgui.glyph`, a character grid plus one
  scene per shipped size. `asciipng` renders it and
  `tools/make_icons.py` packs the result into
  `logo.ico` (16, 20, 24, 32, 48, 64, 128, 256 -- 20 and 24 are what Windows
  asks for at 125% and 150% scaling), `qspgui/icons/logo{,_big}.xpm`,
  `misc/macos/icon.icns` and `misc/common/icons/qsp.svg`. Every size is drawn
  at its own scale rather than resampled off one master, which is the point of
  pixel art; re-render with:

  ```sh
  python tools/make_icons.py --asciipng /path/to/asciipng
  ```

  `logo.xpm`, the window icon on GTK and macOS, went from 16x16 to 32x32 along
  the way. The toolbar and menu icons are untouched.

- **The build tree is runnable.** `langs/` and `sound/` are staged next to the
  freshly built exe (into `Contents/Resources` for the macOS bundle), so a
  development build behaves like an installed one instead of quietly losing its
  data files.

- **Whole-file I/O and session serialisation moved to `comtools`** (`QSPFileIO`,
  `QSPGameState`). Eight sites open-coded the same `malloc` / read / `free`,
  each with its own subset of the checks — one of them with an early return that
  leaked the buffer when the player was quit from inside a game load. The engine
  takes sizes as `int`, so a file it could never address is now rejected rather
  than wrapping into a small allocation.

- **Path containment moved to `QSPPaths`** in `comtools`, out of `QSPFrame`.
  `ComposeGamePath` and `IsValidFullPath` forward to it. Same behaviour, but it
  is now reachable from a test rather than only from a running frame.

- **The development API's JSON moved to `qspgui/devjson.{h,cpp}`**, out of the
  2300-line `devserver.cpp`. It depends on nothing but `wxString`, which is what
  lets the tests exercise it.

- **C++17**, up from C++11, with `.clang-format` matching the style the player is
  already written in. `qspgui/sound/` is excluded — reformatting vendored
  single-header libraries would make every upstream update a merge conflict.

- `--dev-port` out of range is now an error rather than a silent wrap, and
  `--dev-token` on its own turns the API on the way naming a port does.

- QSP code generation (`ToQspLiteral`, `IsValidVarName`, `ToQspIndex`,
  `BuildAssignment`) moved from a private block in `frame.cpp` to `QSPCode` in
  `comtools`. It was behind `QSPGUI_USE_WEBVIEW` and is now shared with the
  development API, so escaping and name validation have one implementation
  rather than one per caller.

### Fixed

- **The panes changed shape every time the window did.** wxAUI gives a dock a
  size in pixels and then leaves it there, so every pixel a resize adds goes to
  the description in the middle: a layout set up on a small window turns into a
  thin strip of actions and objects around a vast page on a large one, and the
  proportions are different again at every size in between. The docks are now
  given back the share of the window they had (`QSPDockLayout` in `comtools`,
  driven from `QSPFrame::OnSize`), so what the player set up is what they keep at
  any size, up to and including maximized.

  The shares are remembered rather than recomputed from the pixels on every
  step, because rounding to whole pixels through a slow drag would otherwise
  walk the layout away from where it started; a dock whose size changed behind
  our back — the sash was dragged — is measured again instead. The input row is
  left out of it: it holds one line of text, and a line of text does not get
  taller because the window did.

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

- **One browser instance per pane.** Five panes now host a `wxWebView`, and each
  Edge instance costs a renderer process. They share one environment, so the
  browser process is shared, but the memory is real — this is the price of the
  whole player being one renderer, and it is worth knowing before profiling a
  memory figure against the classic build.

- **A breakpoint blocks inside the engine's debug callback and runs no event
  loop.** `QSPDevServer::EnterPause` reads the client sockets itself through
  `ServePaused`, because pumping wxWidgets from there re-enters `Dispatch` and
  can destroy a socket an outer frame still holds. Only `IsPauseSafeMethod`
  passes, and the bar there is not "read-only" but "does not call back into the
  interpreter".

- **The shell document is per pane kind**, one file each behind the same shell
  host, each tagged with a hash of its own contents.

### Not yet ported

- `msgdlg` / `inputdlg` still use `QSPTextBox`; they rely on
  `GetInternalRepresentation()` for auto-sizing.
- On GTK and macOS the document base falls back to `file://`, which does not provide
  the containment property described above.
