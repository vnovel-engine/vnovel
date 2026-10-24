# Changelog

Releases of the VNovel libretro core. Each entry says what changed for a user
of the core; per-engine detail is in each engine fork's own `CHANGELOG.md`.

## Unreleased

### Added

- **A development socket for the text stream, in both directions.** With
  `VNOVEL_DEV_PORT=<port>` in the run's environment the core listens on
  127.0.0.1 and serves the same records the text-run log writes — `PAGE(n) …`
  from the look-ahead, `run … draw …` per drawn glyph — as live lines instead of
  a tmpfs file the launcher only flushes at exit. Nothing is lost to a late
  client: records published before one attaches are buffered and released on
  accept (oldest lines dropped first if the backlog overflows). The same
  connection accepts navigation intents:
  - `continue` — advance past a text pause, the way the advance key does. It
    pushes the key through the engine's own input path rather than inventing a
    second advance, so it cannot drift from what a real press does on any
    engine.
  - `select N` — take the Nth choice the current page offers, exactly as a click
    on that label would. It goes through the ENGINE, not the input layers: no
    synthetic mouse event, no coordinate, no hover state — which is what makes
    it work on a page whose pixels the client has never seen. The engine is also
    the side that knows whether the choice exists, and it reports accept/refuse
    back through the bridge (`vnovel_publish_nav_result`) rather than the socket
    assuming.

  Ordering is the reader's: a page reaches the socket when the look-ahead PARSES
  it, which on KiriKiri is before the typewriter has built the message layer's
  link set, so a `select` sent then is refused (correctly) and needs a
  `continue` or three first. One consumer at a time, best-effort both ways (a
  peer that stops reading drops records rather than stalling the game), and no
  framing beyond a newline — it is a development tool, not a protocol. Terminal
  client: `vnovel-client/terminal/vnovel-dev.py`.
- **Page flow on the text stream: `<<<lp>>>` / `<<<pp>>>` / `<go>`.** The stream
  is produced by a look-ahead walk, so it runs AHEAD of the game — a consumer that
  renders what it receives shows text the reader has not reached, and choices the
  engine is not waiting at. The engines now publish the edges that close that gap,
  in two kinds, because they are different actions for a consumer:
  `<<<lp>>>` is an in-page line pause (the same page continues below) and
  `<<<pp>>>` a page boundary (the page ends), with `<go>` for the reader acting.
  The kind is not decoration: a page's text carries its own pause markers, so ONE
  page can correspond to SEVERAL engine waits — measured on Fate's disclaimer, 1
  page against 7 waits, because `[l]` breaks a line without ending a page. A
  consumer that cannot tell the kinds apart skips a page or stalls one. (What the
  measurement also settled: a page's waits ALL arrive BEFORE the page does — 7 on
  Fate's disclaimer — because the engine walks its lines and the stream is
  published after. They describe the page at a whole, not one step at a time, so a
  consumer must not replay them as segments.) All three
  engines publish them: ONScripter/PONScripter from their own click states
  (`CLICK_WAIT` vs `CLICK_NEWPAGE`, and their `select`/`btnwait` waits, which is
  why a save list works too), KiriKiri from the polyfill that already sees every
  tag the live game dispatches. The record vocabulary is defined once in
  `src/devsock.h` (`VNOVEL_REC_*`); the engine-side kinds in `src/engine_bridge.h`
  (`VNOVEL_FLOW_*`) — two protocols, two definition sites.
- **The browser client honours those pauses, and can pass them through.** A page
  is published WHOLE and its own text carries the markers where the engine waits;
  the reader's place in it is the typewriter reveal, which stops at each marker
  and goes on when the reader says so. The engine is told LAST, on the reader's
  own step: it is not asked for permission to reveal a page it has already
  published, and asking first would deadlock the very case this exists for. The
  kinds are therefore used for what they can mean at that moment — `<<<pp>>>`
  ends the page the reader is on and `<<<lp>>>` does not. Each kind can be
  un-ticked, which turns it into AUTO-ADVANCE: the client answers the wait itself
  after a configurable delay. It presses repeatedly until the engine's OWN resume
  (`<go>`) is seen, and not merely until it has sent a `continue`, because an
  engine's first press during its own text-out only fast-forwards the typewriter
  (ONScripter sets `SKIP_TO_WAIT|SKIP_TO_EOL` in `keyPressEvent`) — so sending one
  and assuming the wait passed is exactly the mistake this guards against.
- **`resetmenu`/`insertmenu` build the flavour's MENUBAR, as a tree -- and it is
  NOT the right-click menu.** The two systems share a vocabulary and nothing else,
  and conflating them was a real bug: `resetmenu` used to clear `rmenu`, so Le
  sanglot's six right-click entries (save/load/skip/windowerase/lookback/reset,
  declared at `pscript.dat:90`) were deleted and replaced by its 23 options
  entries, flattened and reversed.

  What the commands are, per the reference: `rmenu` declares the right-click menu;
  `resetmenu` announces that a custom menubar is being built; `insertmenu` adds
  one item, and its optional third argument is "how many submenus deep" the item
  goes. So the depth is honoured now, by the core's bar (`src/menu.cpp`), because
  the engine's own list cannot nest -- `RMenuElt` is a flat
  `{label, system_call_no}` -- while the vendored NP2kai menu nests natively
  (`MSYSITEM` carries a child pointer). `SUB` is what opens a submenu: the
  entries after it at a deeper level belong to IT, not to the entry before it
  (Wave's depth-2 options have no depth-1 parent other than `SUB`).

  The core hands a pick back as the GAME's own system-call number and never
  interprets it, so the core does not need to know what "save" means. Verified
  with a dump of the tree the menu library BUILT (`VNOVEL_MENU_DUMP=1` prints it,
  and `menusys_dump` in the vendored library prints its own), plus the new
  `tests/ons/06_menu_tree` fixture, which asserts the nesting line by line.

  Reading that dump also fixed three defects it exposed: the run terminator was a
  SEPARATE row, which `appends()` appends like any other and draws as a blank menu
  row (the last real item must carry `MENU_DELETED`); a heading was marked
  `MENU_GRAY`, which PREVENTS its submenu from opening (`menusys.c` gates
  `childopn` on that flag), so headings are `MENU_NOSEND`; and the root bar's
  entries all carried `MENU_DELETED`, which ended the top-level run after "File"
  -- so the bar had only ever shown one page.

  Codes: `insertmenu`'s vocabulary is the game's, and the engine resolves it
  CASE-INSENSITIVELY (a script writes `END`/`SKIP`/`AUTO`, the engine's table is
  lower case; the literal comparison made playable entries dead). A code the
  engine has no command for is left inert and drawn greyed rather than wired to
  nothing, so the menu does not offer an action that would not happen. Wired to
  real engine features: the three text speeds (the same field the 1/2/3 keys
  write, so the two cannot disagree) and, in ONScripter-EN, `WAVEON`/`WAVEOFF` and
  `CLICKDEF`/`CLICKPAGE`. Deliberately NOT wired: `FULL`/`WINDOW` (the command
  calls raw `SDL_SetVideoMode`, which would fight the frontend in a libretro
  core), `FONT` (a game's menu must not reconfigure rendering), and
  `WAVEON`/`WAVEOFF` under PONScripter (`volume_on_flag` has no consumer there --
  its only use is commented out -- so it would be a silently dead entry).

  The core takes a pick and reports it; the ENGINE acts on its own codes. A pick
  is delivered as the same `system_menu_mode` a real menu selection produces, so
  it routes through the engine's own `executeSystemCall` and cannot drift from it.

  Two ways in: **F12** (now the default binding; it was unbound) and a click in
  the **bottom-right corner** of the frame, for a pointer-only reader. The corner
  click opens the menu and does NOT also reach the game underneath, which needs
  the "menu owns input" decision re-read after the poll.
- **The two protocols are now ONE vocabulary, with the client's as the
  superset.** The engine ABI and the client protocol used to be two lists that
  had to be kept in step by hand, and they had already drifted where it hurt
  most: `VNOVEL_FLOW_LP` and `VNOVEL_EDGE_START` were **both 0**, so publishing
  one where the other belonged compiled, linked, and sent a plausible wrong
  event. Now there is one list of five page events — `VNOVEL_EV_PAGE_START`,
  `_PAGE_END`, `_LINE_PAUSE`, `_PAGE_PAUSE`, `_RESUME` — which the engines
  publish and the client receives under the same names, and the client protocol
  is that list PLUS the records only the shell can produce (`PAGE`, `run`,
  `CHOICE`). One `vnovel_publish_page_event(int)` replaces the two publishers,
  and KiriKiri's `vnovelPublishPageFlow`/`vnovelPublishPageEdge` collapse to one
  `vnovelPublishPageEvent`.
- **`PAGE(<n>) <text>` carries the page's ORDINAL, and page markup carries a
  sentinel byte.** The number is structure, like `CHOICE(<n>)` and `run <n>`: it
  ties a page to the events and choices that belong to it across a stream a
  consumer reads asynchronously. The byte count it replaces was redundant — the
  text is on the same line, so its length is a `strlen` — and a page's END is
  already its own event. The sentinel (0x01, `LA_MARK`) prefixes every markup
  token so a page can never be MANGLED by its own prose: a scenario that literally
  writes `<pause>`, or an unlucky `<` (one real script has one), is text, and only
  a sentinel introduces a mark. The tokens stay readable ASCII, so a page on the
  wire is still legible.
- **The wire protocol is a BUILD option, and page boundaries are their own
  records.** `make DEVWIRE=1` compiles the development socket in; without it
  `src/devsock.c` is an empty translation unit and the engines' publish calls are
  inline no-ops (`src/engine_bridge.h`), so a
  build that ships a game carries no socket code to reach — not a listener that
  is merely switched off. The flag has to reach the engines' own CMake build too
  (`-DVNOVEL_DEVWIRE=ON`), or the KiriKiri shim compiles its publishes to no-ops
  while the core listens: half a protocol, which presents as a socket that never
  says anything. The build tree is tagged with the flag for the same reason the
  build type is — one directory would otherwise hold whichever flavour was
  compiled first, which is exactly how the shim and the core came out with
  different opinions and the link failed on symbols only one side had.
- **`!w<n>` / `!d<n>` timed waits are carried into the page as `<wait:n>`.**
  The walk dropped them, so a script's own pacing vanished from the stream --
  and Le sanglot alone has 3,679 of them, from 15 ms to 3000. They are INLINE
  tokens rather than edge records because the delay belongs at a position in the
  text, between the same two words in any rendering; an edge record would have
  nowhere to say where. A consumer may honour them (pace the reveal) or ignore
  them: the engine has already run the real wait either way, so this is pacing
  information, not a gate the reader must pass. (`!d` is the same wait plus a
  click, so the two are not interchangeable.)
- **A menu's LABELS reach the wire, so a client can name its choices.**
  `CHOICE(<n>) <label>` follows the pause that arms a menu, one per choice in the
  engine's own order, and `<n>` is the ordinal `select` takes. A pause says the
  reader must act but not what they are choosing between, and usually the page's
  text says it — but a menu built from SPRITES has no text label anywhere: the
  option IS an image (`title\\Start.bmp`), so a text client showed a menu of bare
  numbers while the engine waited. Measured on PONScripter's own choice fixture:
  the `select` labels now arrive as `CHOICE(1) … LEFT to page 2A` /
  `CHOICE(2) … RIGHT to page 2B`. Onscripter publishes a `btnwait` sprite
  button's own filename (all the engine retains, and enough to name the option)
  and a text button's text. The labels are published from EVERY reader wait, not
  only `btnwait`: any command that makes a clickable visible has armed an option,
  whatever wait the script then reads it with. The sprite's name comes from
  `AnimationInfo::image_name` -- `lsp`/`lsph` store it through `setImageName()`,
  which fills only that field.
- **Page START and page END are records of their own, separate from the pause.**
  A boundary and a wait are different events: a script that flows from one page
  into the next pauses nowhere, and still ends a page and begins another. Inferred
  from the pause, that game looked continuous; so `<<<ps>>>` (a page began, drawn
  as a rule), `<<<pe>>>` (a page ended, structural) and the existing waits
  (`<<<lp>>>`, `<<<pp>>>`, `<go>`) are now published separately, each engine from
  the one point every page transition passes: ONScripter and PONScripter from
  `newPage()` (placing it in the click path alone missed every caller that goes
  straight there — measured: a select turn published a START with no END), and
  KiriKiri from the clear tags the walk already understands.
- **The browser client's typewriter reveal hides unrevealed text with a MASK, and
  its speed slider means what it says.** Two defects the reader sees and no test
  would: (1) the reveal used `background-clip: text` with a two-stop gradient, so
  the unrevealed side had to be coloured with something -- and with the colour
  left off it simply inherited the normal text colour and read as ordinary text
  that was SLIGHTLY DIMMER than the revealed part, rather than as text that has
  not arrived; giving it `color: transparent` instead would have flattened every
  colour the markup chose (`.speaker`, `.inlink`). A CSS mask controls visibility
  only, so it needs no colour and preserves them. (2) The tick advanced `shown`
  by `Math.round(cps / 30)` characters per 33ms frame, which rounds UP to a whole
  character and so made 30 characters a second the FLOOR of the slider: 4/s ran
  at 30/s and 45/s ran at 60/s. Counting characters against elapsed time is what
  makes the rate real, and the default came down from 45/s to 22/s, which is
  closer to how the games themselves type.
- **`[l]` is a page pause in the walk's token stream.** `TextLookAhead.tjs`'s
  `pauseTags` listed `p`, `wait` and `click` but not `l`, so a page whose lines
  end in `[l]` -- Fate's disclaimer, and most dialogue -- carried NO `<pause>`
  markers while the engine emitted one `<<<lp>>>` per line. Measured: 7 waits
  against a page with zero markers, which left a consumer nothing to stop at and
  typing each page out in one go.
- **A browser client for the text stream** (`vnovel-client/browser/`): the stream
  rendered as a reading column with Knuth-Plass line breaking, a typewriter
  reveal, clickable choices, and controls for column width, font size, leading,
  wrap mode, justification, hyphenation and reveal speed. `serve.py` bridges the
  core's one permitted TCP consumer to any number of browser tabs (Server-Sent
  Events out, POST in) and replays a backlog to a tab that attaches late; the
  page, its layout code and the vendored line breaker (tex-linebreak, MIT) are
  self-contained, and the bridge is stdlib-only so there is nothing to install
  before a dev tool can run. The line breaking is the vendored library rather
  than this repo's own `src/wrap.cpp`: the same total-fit core, plus glue
  stretch/shrink with real adjustment ratios and penalty costs.
- **`tools/run-sanglot.sh`**, a launcher for Le sanglot des cigales
  (PONScripter) with the socket and optionally the browser client wired up. It
  inherits `DISPLAY` (unlike the Xvfb-based `krun.sh`/`onrun.sh`, since the point
  is to watch text) and passes `--appendconfig` with `pause_nonactive = "false"`
  so the stream does not freeze when the window loses focus. The game runs
  UNPATCHED: reach text with `ADVANCE=N`, by clicking, or through the socket.
- **Page accumulation for the text plane.** KiriKiri publishes its text one
  *line* at a time — each publication is a line-height box — while ONScripter and
  PONScripter publish the whole (screen-sized) text surface at once. A consumer
  that wants "the page" therefore could not just keep the latest publication: on
  KiriKiri that is only the most recent line. The engines can now report where
  each published surface sits in the window (`plane_x`/`plane_y`, via the bridge's
  `vnovel_request_text_plane_splice`), and the core accumulates every publication
  into its own page buffer at that position. One code path: on the two
  page-publishing engines the placement is 0/0 by definition, so the formula
  collapses to "the plane is the page". Off unless a consumer asks, and the
  engines skip the per-publication work when it is off.
- **A regression test for the captured text plane.** The suite asserted nothing
  about the plane, so it stayed green with capture entirely broken — which is
  how the bottom-up-copy crash below reached a real game. The `textplanedump`
  script command (ONS and PON) asks the core to write its copy of the plane to
  a BMP in the save directory, and the harness compares it like any other
  snapshot. `tests/ons/03_text` now asserts the plane's pixels against a
  reference.
- **A `Text Debug View` option** (`off`, `pip`) for working on the capture: a
  translucent picture-in-picture in the bottom-right showing the captured text
  accumulated into a page — the dialogue alone, no artwork. It is a view only,
  and it is what requests page accumulation. See CONTROLS.md.
- **Text look-ahead: a page's text reaches the core before the engine draws it.**
  A walk ahead of the typewriter publishes each page through the bridge as a
  `VNOVEL_RUN_PARSE` run — text, `<pause>`/`<clear>` tokens, `<link:PAYLOAD>`
  labels, and `<<choice>>`/`<<flow>>` where determination ends — on all three
  engines: KiriKiri (a TJS walk hooked into `KAGParser.getNextTag`), ONScripter
  and PONScripter (a shared templated C++ walk in each run loop). Verified on Fate
  (`PAGE(832)` is line 1 of the run log, the first drawn glyph line 2) and on the
  ONS/PON fixtures.
- **The test harness checks that the core contains the KiriKiri engine.** A plain
  `make` after `make KRKR_ENABLE=1` overwrites the same `vnovel_libretro.so` with
  an ONScripter/PONScripter-only build; the KiriKiri test leg then failed with a
  symptom ("engine never reached KAG") that named the scenario rather than the
  cause. It now reports `has no KiriKiri engine — rebuild with KRKR_ENABLE=1`.

### Added

- **Per-game text drivers for the look-ahead.** The greedy walk reads text from
  `ch`/`r`, which is right for a title that writes dialogue inline and is why
  Fate/stay night needs nothing, but a title whose dialogue is MACRO-FORMATTED
  emits no `ch` at all — Fata Morgana's lines arrive as `[emb exp=mp.text]`
  inside a macro body — so the walk published nothing for it. A driver is the
  seam: `vnovelRegisterTextDriver(["<game id>"], fn)` returns text, page
  metadata, a stop, or "not mine", is consulted ONLY for tags the generic walk
  declined (so it can add to the dialect and never change what already works),
  and ships as `scenario/vnovel_textdriver.tjs` in the game's own override tree
  — data, not a branch in a shared loop. Absent, the generic walk applies
  unchanged: a title with no driver is unaffected.
- **A speaker token, `<<speaker:NAME>>`,** emitted inline where the speaker
  changes. In the page's existing token stream rather than a new field in the
  run ABI, so no consumer has to version anything: one that does not know the
  token still parses the page and loses only the name. Fata Morgana is the first
  producer.

### Added

- **KiriKiri games that ship as ONE executable with the archive embedded.** A
  title may distribute as a single .exe with the XP3 appended after its last
  section (the engine's own `XOPT_EMBED_AREA_` marker); such a game has no
  `data.xp3` on disk, so engine detection decided "not KiriKiri" and the run died
  in ONScripter with "no game script found". The core now recognises the layout
  and hands the executable to the engine, which reads the archive IN PLACE — no
  extraction, no copy of a multi-hundred-MB archive. Verified on 'NORA! V1.02',
  which boots to its title screen.

### Fixed

- **The text stream no longer desynchronizes from the text that is playing.**
  Three separate causes, each measured on Fate (KiriKiri) before and after:
  - **KiriKiri pages carried no markup sentinel.** The client's tokenizer reads a
    markup token only when the `0x01` sentinel precedes it (`LA_MARK`, the ONS walk
    emits it), and the TJS walk did not — so every Fate page arrived with bare
    `<pause>`/`<clear>`/`<<choice>>`, which a conforming consumer reads as PROSE:
    the literal characters rendered mid-sentence and no page's waits recognized.
    Measured: Fate's pages carried 0 sentinel bytes against 4–6 marks each. The
    walk now routes every token through one `mark()` helper, so a site cannot
    forget the byte again.
  - **A page with no visible character was published as a page.** A run of
    newlines or a stretch of indentation is structure, not content — but both
    walks counted it as text, so Fate emitted pages like `\n\n\n\n\n<<flow>>` and
    `      `. A consumer shows one page per engine pause, so each such page
    consumed a pause slot and shifted every page after it. Both walks now require
    one visible character (ONS/PON already had the rule for a bare newline; the
    space case and the whole KiriKiri side were missing it). Measured: with the
    residue pages dropped, the published pages and the pages the engine actually
    DREW align 1:1 in order — 21 of 21, against 4 of 20 before.
  - **The outbound ring could drop a page while keeping its glyphs.** The KiriKiri
    stream is dominated by per-glyph `run` records (~65× the PAGE bytes in one
    measured run, 242 KB against 3.7 KB), and the 64 KiB backlog ring evicted
    oldest-first, so an overflow — which is exactly what a briefly-stalled client
    causes — could take the sparse PAGE/flow/CHOICE records that pair a page with
    its pause, while the glyphs that cannot be paired without them stayed. The
    ring now sheds `run` bulk first and touches the rest only when nothing bulk is
    left. Covered by a unit harness, `make devsock-test`, which fills the ring
    with no consumer attached — a live run cannot force the overflow, because the
    kernel's own buffer swallows the data first — and asserts that every PAGE
    survives AND that glyph bulk was dropped; it fails (10/400 pages) against the
    old rule.
  - **`VNOVEL_TEXT_RUN_LOG` was ignored whenever the socket was listening.** The
    sink opened the log only when no listener existed, so a run with BOTH sinks
    configured wrote no records to the log at all — the one combination that lets
    you compare the two transports, and therefore the one that produced nothing to
    compare. The two are now independent: either, both, or neither, with the same
    records on each. (The event and choice publishers had the mirror of the same
    bug: they wrote the log only when it happened to be already open, so a run's
    opening page events reached the socket and never the log.)


- **The look-ahead published the engine's own text markup.** ONScripter's
  `readToken` replaces a leading `>` with `TXTBTN_END` (0x04), a `<` with
  `TXTBTN_START` (0x01), and a ruby-free `(`/`)` with `LEFT_PAREN`/`RIGHT_PAREN`
  (0x02/0x03) — engine sentinels defined at `ScriptHandler.h:376-382` and consumed
  by the renderer as markup. The walk published all four verbatim, so every text
  line of the ONS fixtures carried a leading 0x04 on the wire. Invisible in the
  game (the renderer drops them) and invisible to anything that only looked at
  ASCII pages, which is why it survived: it is text the reader never sees, in a
  dimension markup should not reach. Dropped in the walk now, alongside the text
  marker it already stripped.
- **The ONScripter/PONScripter look-ahead shredded Japanese text.** The
  inline-command consumer scanned a line BYTE BY BYTE, deciding character length
  by "is this byte >= 0x80" — false for Shift_JIS, where a two-byte character's
  trail byte can be ASCII range (40 CP932 kanji end in `@`, 40 in `\`). A trail
  byte equal to a command character was consumed as that command, the text marker
  was dropped, and the orphaned lead byte was then paired with the next page byte
  and rendered `?`. Measured on 'Boku To Iu Mono' (the first Japanese ONS script
  the walk had seen; every fixture is ASCII, where the assumption holds). The
  walk now tests for commands only at ASCII positions and decodes multibyte
  characters whole, using the engine's own decoder for the length.
- **The scenario auto-path assumed `data.xp3`.** It was `cwd + "/data.xp3"`,
  which names a file that does not exist for an embedded-archive title, so
  opening a scenario failed with `Cannot find storage .../data.xp3`. It now
  derives from the project directory the engine actually selected, which is
  correct for both layouts.
- **The look-ahead's macro-argument stack never rewound.** The parser pushes
  macro arguments at `KAGParser.tjs:1667`, which is not behind the
  `processSpecialTags` gate, but the matching `macropop` pop IS — and the walk
  blanks that gate to see control tags as data. So the walk's stack grew without
  bound and `getMacroTopNoAddRef()` kept returning a stale frame, which broke
  every expression evaluated against `mp` (a macro's own arguments). Generic:
  every macro-using title leaked, not just the one that surfaced it. The walk now
  balances the pop itself.
- **A look-ahead double free on any ONScripter script with a `clickstr`.** The
  walk parses a private handler that *borrows* the live script image
  (`ScriptHandler::borrowFrom`). It correctly shared `script_buffer` and
  `label_info`, and correctly forgot them before destruction — but not
  `clickstr_list`, which borrowFrom also shares (it points into the borrowed
  script). `reset()`, called from the private handler's destructor, does
  `delete[] clickstr_list`, so every walk freed the LIVE handler's clickstring
  list; the engine's later use, or its own destructor, then freed it again:
  `free(): double free detected in tcache 2`, aborting the process. Reproduced
  on Tsukihime (whose `*define` sets `clickstr` before the first walk) and fixed
  by also clearing `clickstr_list` in `unhookBorrowedScript()`. PONScripter is
  unaffected — it stores the clickstring list as a `std::set` it copies.
- **The engine was silently built with its debug probes on.** KiriKiri's probe
  flag is a CMake cache value, so once a run passed
  `KRKR_CMAKE_EXTRA="-DOPTION_DEBUG_PROBES=ON"`, every later build reused the
  cached ON and compiled the engine's ad-hoc stderr probes into a release build
  with nothing in the output saying so. The flag is now pinned `OFF` on every
  configure (`KRKR_CMAKE_EXTRA` still wins if set explicitly).
- **A page is published once, and only if it has text.** The ONScripter and
  PONScripter walk recorded its covered position *after* restoring the
  interpreter, so it stored where the walk entered rather than where it stopped:
  every line inside the page re-entered the walk and re-published the page — up
  to ~50 copies, each one line shorter. The KiriKiri walk published a text-free
  `<<flow>>` page for every control-flow tag it stopped at, putting eight empty
  pages ahead of the real first page during the game's boot. Both fixed; a page
  with no characters is not a page (a `<<choice>>` with no text still is).
- **`TextLookAhead.tjs` had no UTF-8 BOM**, unlike every sibling polyfill. The
  engine reads a BOM-less script as Shift_JIS, so the file's UTF-8 Japanese
  comments were misdecoded and one sequence made the whole file fail to load —
  silently disabling the entire look-ahead. BOM added, matching the others.

- **The ONS/PON look-ahead parses a private copy, not the live engine.** It used
  to run the engine's tokenizer on the running handler, which perturbed the game
  and forced hand-written save/restore of the token buffer, the command type and
  the interpreter position. It now parses a second handler that borrows the
  script image read-only (`borrowFrom`/`unhookBorrowedScript`), the same shape
  KiriKiri's look-ahead already used (`assign(live)`). The live interpreter is
  never touched.
- **The look-ahead consumes the engines' inline text commands.** A text line
  carries control codes the renderer never draws (`@` click, `\` new page,
  `!sd`/`!w<n>`/`!d<n>` waits, `#rrggbb` colour, the encoding's text marker). The
  walk now consumes them the way the renderer does instead of publishing them as
  dialogue (`!sd`, `!d15000` appeared verbatim before), and emits `<pause>`/
  `<clear>` at the reader-facing ones.

- **KiriKiri published each glyph one call too early.** `TVPPresentTextPlane`
  ran *before* the glyph was blended into the surface it describes, and the host
  copies that surface synchronously, so the last glyph of every publication was
  missing — a captured line read garbled while the frame it came from was
  clean. The publish now happens after the blend, on the single path every
  branch of that function reaches.

- **The text plane crashed on a large bitmap.** The engine's bitmaps are
  bottom-up and the pointer it publishes is the *last* row, so copying
  `pitch × height` bytes forward from it read past the end of the allocation.
  Small planes survived by luck; Fate/stay night's prologue segfaulted inside
  `memcpy`. The copy now walks the signed stride a row at a time.
- **The menu bar covered the top of the picture.** It was drawn whenever a game
  asked for a menu, and Fata Morgana does ask — but in KiriKiri that request is
  about a *window* menu bar, which this target has nothing to attach to. The
  reference engine answers it by creating a menu window and never mapping it.
  The bar is now drawn only when the hotkey asks for it, on a key you choose.

### Changed

- **The menu bar's hotkey is a core option** (`Menu Hotkey`), and is **unbound
  by default** — a title that never asks for a menu bar no longer has a key
  bring one up. Replaces the hardcoded F12.
- **The menu is the core's own, and no longer tied to `menu.visible`.** A game's
  window-menu request and this overlay are unrelated; wiring one to the other
  meant the overlay could only be reached if a game happened to ask for a bar.
  `MenuItem.visible` keeps its item-level meaning, which is what the scripts
  actually use.
- A **Session** page reports the running session: the loaded archive and its
  identity, the engine, the era option as set, the build string, where saves
  land, and the override directory with whether it is even present.

## 0.3.0 — 2026-10-16

Runs pre-2.23-era KiriKiri titles (Fate/stay night first) and adds the
text-plane seam. Per-engine detail is in each engine fork's own `CHANGELOG.md`.

### Added

- **Text-plane rendering.** A game's text is now captured into a surface of its
  own and published alongside the frame, instead of existing only inside the
  picture. All three engines publish through one engine-neutral seam
  (`TVPSetTextPlaneCallback` → `krkr_text_plane_cb` → the core's sink), and
  `KRKRSDL2_TEXT_PAGE=1` presents the captured page in place of the frame. The
  purpose is that text can be restyled, reflowed, scrolled or shown a page at a
  time independently of the artwork — what a reader or a page-based (NDS-class)
  port needs. KiriKiri publishes from the single funnel every glyph is drawn
  through, so no "which layer is the text layer" rule is needed; ONScripter-EN
  and PONScripter publish their sentence text.
- **A native menu.** F12 toggles a menu built from the game's own `MenuItem`
  tree, and `menu.visible` set from script drives it (both directions). The
  menu is the Win32-like `MenuItem`/`Window.menu` contract the scripts already
  expect, backed by a vendored menu implementation. It is composited at the
  present boundary and never composed into the engine's rendering layer, so the
  engine's framebuffer and dirty-rect tracking are left alone.
- **Per-game engine compatibility.** The KiriKiri engine era is chosen per game
  from a table keyed by the game's identity — the crc32 of its `data.xp3`, the
  same fingerprint `vnovel.rdb` and `tools/hash-game.py` use — with `2.19` and
  `modern` presets. Every switch stays individually settable and a level is only
  a named preset, because a published title can carry one backported behaviour
  without the rest. `auto` (the default) consults the table; an explicit
  `2.19`/`modern` overrides it for the session and `KRKRSDL2_COMPAT_<id>` pins a
  single title without a rebuild. Each run logs which source supplied the mask.
- **Save-format compatibility.** The `structconst`/`structcrlf` switches make
  the engine write the save wire format a pre-2.23 title expects; they are bound
  as one format, because a file carrying one era's prefix and the other's
  newline is readable by neither. A Fate/stay night save written by the core is
  byte-identical to the original engine's, and the original engine loads it.
- **Visual-effects shims.** The Windows DLLs a title loads for effects —
  `drawer.dll`, `firespark.dll`, `extrans.dll` — are replaced by built-in CPU
  shims, selected by flag when a script would load a DLL. Scenes that called
  these names previously threw and aborted the scenario.
- **A user resource-override directory** (`<gamedir>/.overrides/`) for patches
  and replacements. Precedence is user > game files > TJS shims, and overrides
  are applied unconditionally as the highest priority.
- **A debug facility.** `make debug` builds a separate tree with the same
  feature set, with debug symbols and the engine's diagnostic probes; the core
  exposes the probe flags and the engine's `-forcelog` important-log sink, so
  compatibility provenance, override disposition and probe output are readable
  from `<datapath>/krkr.console.log`.

### Fixed

- **Clicks after the first were delivered to the wrong layer.** The shift/button
  flags posted with a mouse event were sampled *before* the button transition was
  recorded, so a button-up still claimed the button was held. The layer manager
  consults exactly that bit to release mouse capture, so capture was never
  released and every later click routed to the first layer clicked. Link clicks
  now land on the layer under the cursor.
- **The message text was overpainted by an opaque fill** under the modern
  layer-compositing algorithm. The pre-2006 algorithm is now selectable (the
  `2.19` preset), which drops that fill.
- **FAudio asserted** when the device requested a 1024-frame quantum; the mixer
  accepts it.
- **The ONS/PON shadow buffer was sized wrong**, which could corrupt or drop
  frames at a forced resolution.
- **User overrides never applied.** The override-directory walker widened each
  file name through `std::wstring` and handed it to code expecting `char16_t`,
  so every key collapsed to a stray character and no lookup could match. The
  whole channel was inert.
- A duplicated UTF-8 BOM in `Util.tjs`.
- **The test harness reported false failures**: it resolved the core path
  against the caller's working directory, so running it from anywhere but its
  own directory made RetroArch silently ignore `--libretro`.

### Changed

- `make` and `make debug` carry the same feature set; debug and release objects
  and binaries no longer share a tree.
- KiriKiri input is delivered through the core's own clock, and the engine's
  `-forcelog` logging is enabled per run.

## 0.2.0 — 2026-10-11

First release under the **VNovel** name. The core was called
`onscripter-en-libretro` when it covered ONScripter-EN alone; it now covers the
three engines most visual novels are built with, so the name follows.

### Added

- **Game identification.** The core ships a libretro database (`vnovel.rdb`)
  that names known games by the hash of the script file it already uses for
  engine detection — `nscript.dat`, `pscript.dat` or `data.xp3`. RetroArch
  scans then show *Tsukihime* instead of `nscript.dat`.
  - `tools/hash-game.py` fingerprints a game folder and derives its name the
    way the engines do (`game.id`, `ons.cfg gameid=`, `;gameid`, `versionstr`,
    `caption`; KiriKiri falls back to `setup.ini` and the launcher name).
  - `tools/make-rdb.py` writes the database; `make rdb` regenerates it.
  - Contributions are collected in
    [vnovel-database](https://gitlab.com/matchaxnb/vnovel-database).
- **KiriKiri (KAG) engine**, so Fate/stay night and The House in Fata Morgana
  run alongside the ONScripter and PONScripter titles.
- An **installer** (`install.sh`) that places the core, its runtime, the engine
  modules and the database where a frontend finds them.
- Releases are now built and published by CI for Linux x86_64, Linux aarch64
  and Windows x86_64.

### Changed

- The core, its artifacts, its core-info file and its RetroArch core options
  are named `vnovel` (`onscripter_en_*` options became `vnovel_*`). **Existing
  per-core option settings are not migrated**: RetroArch keeps them under the
  old core name, so the options return to their defaults on first run.
- **Save files move.** RetroArch isolates saves per core using the core's
  library name, so the save directory becomes `…/saves/vnovel/` (previously
  `…/saves/onscripter/`), with a per-game subdirectory inside it. Copy the old
  game folder across to keep your saves.
- The C/C++ sources moved to `src/`; the build still runs `make` from the
  repository root.

### Fixed

- **Windows text rendering** and asset-override handling; saves are now
  durable on storage that drops a create-and-write in one open.
- **MP3 BGM was silent** on both ONScripter-EN and PONScripter: a priming MPEG
  stream legitimately returns no audio for its first buffers, which the
  engines read as "track finished" and restarted the music before it played.
- **PONScripter could crash** (SIGSEGV) while skipping and abort on reset.
- **KiriKiri input was doubled** — a pad press reached the game twice, and
  polled input (`System.getKeyState`) saw keys as released.
- Fast-forward time dilation froze in KiriKiri, stalling timed events while
  frames raced ahead.
- `valid_extensions` now includes `xp3`, matching the core-info file, so a
  frontend will load `data.xp3` directly.

## 0.1.0-pre1 — 2026-10-05

Pre-release. Four-module core (core + runtime + ONScripter-EN and PONScripter
engine modules), built for Linux x86_64/aarch64 and Windows x86_64/i686, with
the KiriKiri engine available separately.
