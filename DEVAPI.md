# Development API

A loopback socket that lets an external editor reload a running game, execute code
and inspect state, so a change can be seen without the usual save / update / load /
navigate dance.

```
qspgui.exe --dev [--dev-port 4747] [--dev-token secret] game.qsp
```

On startup the player prints the bound port to stdout:

```
qspgui: development API listening on 127.0.0.1:4747
```

The server binds the loopback interface only and is never started without `--dev`.
With `--dev-token`, a client must send `hello` with a matching token before any
other command is accepted.

## Transport

JSON-RPC 2.0, one object per line, UTF-8, `\n` terminated, over plain TCP.

Requests carry an `id` and get a matching response; requests without one are
treated as notifications and answered with nothing. The server also pushes
notifications of its own at any time, so a client must read messages in a loop and
match responses by `id` rather than assuming the next line is its answer.

```jsonc
--> {"jsonrpc":"2.0","id":1,"method":"exec","params":{"code":"gold = 100"}}
<-- {"jsonrpc":"2.0","id":1,"result":{"ok":true,"loc":"room"}}
<-- {"jsonrpc":"2.0","method":"refreshed","params":{"isNewDesc":false,"loc":"room"}}
```

Errors use the standard envelope; the message is the engine's own error text with
its code and location:

```jsonc
<-- {"jsonrpc":"2.0","id":7,"error":{"code":-32000,
      "message":"[23] Location not found! (line 1)"}}
```

## Reloading a game

`reload` is the point of the whole thing.

```jsonc
{"method":"reload","params":{
   "path":"C:/games/mygame.qsp",  // omit to re-read the file already open
   "data":"<base64>",             // or hand over the bytes directly
   "keepState":true,              // default
   "reenter":"goto"               // goto | gosub | code | none
}}
```

It answers with what actually happened:

```jsonc
{"ok":true,"locations":42,"loc":"room","previousLoc":"room",
 "stateRestored":true,"reentered":true}
```

### What it does, and why it is built this way

The engine has no way to patch a single location. `qspLocs` is private to the
library, and loading a world with `isNewGame = QSP_FALSE` is the `INCLUDE` path,
which *skips* names that already exist instead of replacing them. What does work
is a round trip through a save:

1. Set `DEBUG = 1`. `qspCheckGameStatus` only verifies the game CRC when `DEBUG`
   is zero, so this is what lets a snapshot taken against the old `.qsp` load into
   the new one.
2. `QSPSaveGameAsData` into memory — all global variables, objects, playlist, and
   the current location **by name**.
3. `QSPLoadGameWorldFromData(..., QSP_TRUE)` — swaps the world. Variables are not
   touched by this.
4. `QSPOpenSavedGameFromData` — restores the snapshot. The current location is
   resolved by name against the *new* world.

### Things to know before relying on it

- **The screen is stale until the location is entered again.** A save carries the
  rendered description and the literal code of the current actions, which is
  exactly why old saves keep working after a location is deleted from the `.qsp`.
  `reenter` decides what to do about it:
  - `goto` (default) — `GOTO` the current location, so the new code runs and the
    description and actions are rebuilt. **Its side effects run again**: a
    location body that spends money or advances a counter will do so a second time.
  - `code` — run the location body via `QSPExecLocationCode` without navigating.
  - `gosub` — `GOSUB` it.
  - `none` — swap the world and the state, leave the screen alone.
- `ONGSAVE` and `ONGLOAD` fire, because the round trip is a real save and load.
- `DEBUG` stays at `1` afterwards. Set it back yourself if the game reads it.
- If the current location no longer exists in the new world, `loc` comes back
  empty and `reentered` is `false`.
- A failed restore falls back to `QSPRestartGame` rather than leaving the session
  half reloaded, and says so through `stateRestored:false` plus a `warning`.

## Commands

| method | params | result |
| --- | --- | --- |
| `hello` | `token` | `authorized` |
| `ping` | — | `player`, `engine`, `protocol` |
| `state` | — | `gameOpened`, `gameFile`, `loc`, `execLoc`, `lineNum`, `desc`, `vars`, `actions[]`, `objects[]` |
| `locations` | — | `count`, `names[]` |
| `locationCode` | `name` | `desc`, `code[{lineNum,line}]`, `actions[{name,image,code[]}]` |
| `exec` | `code`, `refresh` | `ok`, `loc` |
| `eval` | `expr`, `type` (`str`\|`num`), `refresh` | `type`, `value` |
| `getVar` | `name`, `index` | `type`, `value`, `count` |
| `setVar` | `name`, `value`, `index`, `refresh` | `ok` |
| `vars` | `names[]`, `filter`, `maxValues`, `maxVars`, `includeEmpty`, `rescan` | `count`, `skipped`, `truncated`, `loc`, `vars[]` |
| `varNames` | `filter`, `existing`, `rescan` | `count`, `names[]` |
| `setVars` | `vars[{name,value,index,append}]`, `refresh` | `ok`, `count`, `loc` |
| `watch` | `names[]`, `reset` | `count`, `names[]` |
| `trace` | `enabled`, `lines`, `vars`, `locs[]`, `limit` | `enabled`, `lines`, `vars`, `locs[]`, `limit` |
| `goto` | `loc`, `args[]`, `sub` | `ok`, `loc` |
| `reload` | see above | see above |
| `snapshot` | `slot` | `slot`, `size`, `loc` |
| `restore` | `slot` | `ok`, `loc` |
| `restart` | — | `ok` |

`snapshot` and `restore` keep saves in the player's memory under a name, which is
the cheap way to run the same scene repeatedly: snapshot once, then restore
between attempts instead of replaying up to it.

Variable names are case-insensitive — the engine upper-cases them while
preprocessing code, and so does the server. The `$` prefix is optional.

## Inspecting variables

`vars` is the whole variable table as the engine currently holds it:

```jsonc
{"method":"vars","params":{}}
```
```jsonc
{"count":5,"skipped":0,"truncated":false,"loc":"start","vars":[
  {"name":"GOLD","exists":true,"count":1,"truncated":false,
   "values":[{"type":"num","value":10}]},
  {"name":"INVENTORY","exists":true,"count":2,"truncated":false,
   "values":[{"type":"str","value":"lamp"},{"type":"str","value":"rope"}]}
]}
```

A value is `{"type":..,"value":..}`. `type` is the engine's own: `num`, `bool`,
`str`, `code`, `varref`, or `tuple`, and a tuple's `value` is an array of the
same shape, nested up to eight deep before it is elided as `"..."`.

Names are upper-cased and the `$` or `%` prefix is dropped, because that is how
the engine stores them: `$FOO` and `FOO` are **one variable** whose values each
carry their own type, not two.

`filter` keeps only names containing a substring. `maxValues` caps the items
returned per variable (default 32, `-1` for all) and sets `truncated` on the
ones that were cut; `count` is always the real length. `maxVars` caps how many
variables come back at all, and what it left out is counted in `skipped`.

### Where the names come from

The engine cannot enumerate its variable table: `qspGlobalVars` is private to
the library and nothing in the public API walks it. What the API will answer is
"what is in *this* name", so the player recovers the names from the world's own
source — every token in every location's code and action code that reads as a
variable reference — and then asks about each one. The scan is cached per loaded
world and redone after `reload` or a new game; `rescan:true` forces it.

Two consequences worth knowing:

- **A name that only exists at runtime is not found.** `DYNAMIC`, or a `SET`
  through a computed name, writes a variable that appears nowhere in the source.
  Pass such a name in `names[]` and it is answered normally — the lookup itself
  has no such limit, only the discovery does.
- **An empty variable is indistinguishable from an absent one.** The engine
  hands back a shared empty variable for any syntactically valid name rather
  than reporting "no such variable", so holding at least one value is the only
  evidence a variable exists. `exists` means exactly that, and `includeEmpty`
  asks for the rest anyway. An explicit `names[]` always reports every name it
  was given.

`varNames` returns just the list, for completion. It leaves out the engine's
statement and function names — `GT`, `IF`, `MID` and the rest read exactly like
variable references in source — unless they carried a `$` or `%` sigil, which
settles it. `existing:true` narrows the list to names that currently hold a
value.

## Writing variables

`setVars` applies several writes as one piece of code, so a variable panel does
not run and refresh the game once per edited cell:

```jsonc
{"method":"setVars","params":{"vars":[
   {"name":"gold","value":"999"},
   {"name":"$name","value":"O'Brien"},
   {"name":"$inventory","index":"1","value":"rope"},
   {"name":"$log","append":true,"value":"entered the cave"}
 ],"refresh":true}}
```

Each entry is the same shape `setVar` takes. The engine exposes no setter, so
every write becomes the assignment a game would have written itself, with the
name validated and the value escaped — which is why a numeric variable can only
be given a number, and why `%`-prefixed tuples cannot be written at all. An
absent `index` means item 0; anything that is not a non-negative number is a
string key.

Nothing is executed until every entry has been turned into code, so a bad entry
fails the whole call and names the index it was at.

## Watching variables change

`watch` names the variables to keep an eye on. The player remembers what they
held and reports movement as `varsChanged`:

```jsonc
{"method":"watch","params":{"names":["gold","$name","hp"]}}
```
```jsonc
{"jsonrpc":"2.0","method":"varsChanged","params":{
  "reason":"exec","loc":"start","changes":[
    {"name":"GOLD","index":0,"kind":"changed","old":"10",
     "new":{"type":"num","value":999}}
  ]}}
```

`kind` is `changed`, `added` or `removed`. `new` is a typed value; `old` is the
previous value as text, because text is all that is kept between checks — two
values that print alike are treated as equal, which is enough to drive change
highlighting and far cheaper than a deep compare.

The watch is checked whenever the engine goes idle, so a change made by a timer,
an action or a `SLEEP` is reported without being asked for. `{"reset":true}`
re-reads the current values without reporting, so the next comparison is against
now. Opening or reloading a world resets the baseline too.

## Streaming execution

`trace` turns on the engine's line-level debug hook. Every executed line arrives
with the location and the line number it is at — which is what puts a marker in
an editor and moves it as the game runs.

```jsonc
{"method":"trace","params":{"enabled":true,"lines":true,"vars":true,
                            "locs":["north"],"limit":2000}}
```
```jsonc
{"jsonrpc":"2.0","method":"trace","params":{"dropped":0,"events":[
  {"loc":"north","actIndex":-1,"lineNum":1,"line":"VISITS = VISITS + 1"},
  {"loc":"north","actIndex":-1,"lineNum":6,"line":"TALLY = TALLY + I",
   "changes":[{"name":"TALLY","index":0,"kind":"changed","old":"15",
               "new":{"type":"num","value":16}}]}
]}}
```

`lineNum` and `actIndex` mean what they do in an `error` notification, so the
same code that puts a marker on a failing line puts one on the executing line.
Lines come back upper-cased and with their `<<...>>` intact: that is the
preprocessed form the engine runs, not the source text — `locationCode` has the
source.

With `vars:true` each event carries the watched variables that moved on that
line, which is the point of the whole thing: the change and the line that caused
it arrive together and in order, rather than being correlated after the fact.

### What tracing does and does not see

- **Only location code.** The engine fires its debug hook for code it knows a
  line offset for. Code run through `exec`, `DYNAMIC` or `DYNEVAL` has none, so
  it executes untraced. The engine's own `DEBUG` variable is unrelated to this
  and is not touched.
- **`locs[]` narrows it to named locations**, matched case-insensitively, empty
  for all. This is not just noise reduction — an unfiltered trace of a real game
  spends the whole budget on code nobody is looking at. Filtered lines cost
  nothing and are not counted as dropped.
- **Events are batched, not sent one per line.** A loop can run tens of
  thousands of lines between two turns of the event loop; one notification each
  would spend longer in the socket than the interpreter spends running them. A
  batch is flushed once the engine goes idle.
- **The batch is capped** at `limit` events (default 2000). Past that, lines are
  counted in `dropped` rather than queued, so a runaway loop costs a counter
  instead of memory. A 20,000-iteration loop traced unfiltered reports 2000
  events and 58,002 dropped, and the game finishes normally.

Turning tracing off keeps `locs`, `lines`, `vars` and `limit` as they were, so
toggling it around a step does not quietly widen what is traced. Tracing also
survives `reload`, since the engine only clears debug mode at startup.

Reading state from the hook is safe — a value lookup is a hash lookup and runs
no game code — but nothing is *sent* from there. Events are queued and written
once the interpreter is off the stack, for the same reason commands are.

## Notifications

| method | params |
| --- | --- |
| `welcome` | `player`, `engine`, `protocol`, `authRequired` — sent on connect |
| `refreshed` | `isNewDesc`, `loc` |
| `error` | `code`, `desc`, `loc`, `actIndex`, `topLineNum`, `intLineNum`, `line` |
| `message` | `text` — whatever `MSG` displayed |
| `gameOpened` | `path`, `locations` |
| `varsChanged` | `reason`, `loc`, `changes[]` — see above |
| `trace` | `dropped`, `events[]` — see above |

`error` carries enough to put a marker on the failing line in an editor:
`loc` plus `topLineNum` locates it in the location's code, and `actIndex` says
whether the failure was in the body (`-1`) or in one of the base actions.

## Threading and re-entrancy

The engine is not re-entrant, and it yields to the event loop while game code is
still on the stack — a `SLEEP`, an input or message dialog, a menu, a forced
refresh. Commands arriving in those windows are queued and run from idle once the
engine is out. Nothing in the API calls into the engine off the GUI thread.
