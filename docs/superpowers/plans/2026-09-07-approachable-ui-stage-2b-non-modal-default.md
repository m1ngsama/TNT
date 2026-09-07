# Approachable UI — Stage 2b: Non-Modal Default Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A person who has never used vim can join, type, scroll, and run commands without learning a mode. A person who wants vim keys gets them back in one step.

**Architecture:** A `keymap` module answers "what does this key mean" for the handful of keys whose meaning differs between the two keymaps. The default keymap never leaves INSERT: the keys that used to switch modes now scroll, dismiss, or insert. `MODE_NORMAL` and `MODE_COMMAND` stay in the code and stay reachable — from the vim keymap only.

**Tech Stack:** C11, no new dependencies.

**Spec:** `docs/superpowers/specs/2026-09-07-approachable-ui-design.md` (Stage 2, second half)

## Deviation from the spec, and why

The spec described two full byte-sequence-to-intent tables replacing the key
dispatch. Reading the code changed the shape of the work: "non-modal" is
reachable by making the default keymap never enter `MODE_NORMAL`, rather than
by removing the modes. Same user-visible result, a fraction of the risk, and
vim mode keeps its current implementation rather than a reimplementation that
would have to be proven equivalent key by key. The `keymap` module still exists
and is still a pure, unit-tested table; it just answers for the divergent keys
instead of for every byte.

## Global Constraints

- C11, `-Wall -Wextra`, no new third-party dependencies.
- **Vim mode must remain key-for-key identical to today.** `test_interactive_input.sh` and the new vim regression test are the evidence.
- The default keymap must never enter `MODE_NORMAL` or `MODE_COMMAND`.
- TNT is anonymous, so no preference can be persisted; every enable path is per-session or server-wide.
- New module ships header, source, unit test, Makefile entry, MAINTAINERS entry, and a `.gitignore` line.
- `make release-check` must pass. Work lands through a pull request into `main`.

## Landing order

Three commits, in this order, so the risky one is small and revertable:

1. `keymap` module plus the three enable paths, **with the default still vim** — zero behaviour change, everything in place.
2. The default keymap's behaviours, reachable only by opting in.
3. **Flip the default**, update the docs. One commit, easy to revert.

---

### Task 1: The `keymap` module and the enable paths

**Files:**
- Create: `include/keymap.h`, `src/keymap.c`, `tests/unit/test_keymap.c`
- Modify: `include/ssh_server.h` (a `tnt_keymap_t keymap` field on `client_t`), `src/bootstrap.c` (login-name signal), `src/main.c` (`--keymap` flag), `src/input.c` (session init), `tests/unit/Makefile`, `MAINTAINERS`, `.gitignore`

**Interfaces:**
- Produces:
  - `typedef enum { TNT_KEYMAP_DEFAULT, TNT_KEYMAP_VIM } tnt_keymap_t;`
  - `tnt_keymap_t tnt_keymap_from_name(const char *name, tnt_keymap_t fallback)` — accepts `"default"`, `"vim"`; anything else returns `fallback`.
  - `tnt_keymap_t tnt_keymap_from_login(const char *ssh_login, tnt_keymap_t fallback)` — an SSH login name of `vim` (case-insensitive) selects the vim keymap; every other name returns `fallback`.
  - `const char *tnt_keymap_name(tnt_keymap_t keymap)`
  - `bool tnt_keymap_uses_modes(tnt_keymap_t keymap)` — true only for vim.

- [ ] **Step 1: Write the failing test**

```c
TEST(keymap_names_round_trip) {
    assert(tnt_keymap_from_name("vim", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_VIM);
    assert(tnt_keymap_from_name("default", TNT_KEYMAP_VIM) == TNT_KEYMAP_DEFAULT);
    assert(strcmp(tnt_keymap_name(TNT_KEYMAP_VIM), "vim") == 0);
    assert(strcmp(tnt_keymap_name(TNT_KEYMAP_DEFAULT), "default") == 0);
}

TEST(keymap_unknown_name_keeps_the_fallback) {
    assert(tnt_keymap_from_name("emacs", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_DEFAULT);
    assert(tnt_keymap_from_name("", TNT_KEYMAP_VIM) == TNT_KEYMAP_VIM);
    assert(tnt_keymap_from_name(NULL, TNT_KEYMAP_VIM) == TNT_KEYMAP_VIM);
}

TEST(keymap_login_name_selects_vim) {
    assert(tnt_keymap_from_login("vim", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_VIM);
    assert(tnt_keymap_from_login("VIM", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_VIM);
    /* An ordinary login must not change the keymap. */
    assert(tnt_keymap_from_login("alice", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_DEFAULT);
    assert(tnt_keymap_from_login("", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_DEFAULT);
    assert(tnt_keymap_from_login(NULL, TNT_KEYMAP_VIM) == TNT_KEYMAP_VIM);
}

TEST(keymap_only_vim_uses_modes) {
    assert(tnt_keymap_uses_modes(TNT_KEYMAP_VIM));
    assert(!tnt_keymap_uses_modes(TNT_KEYMAP_DEFAULT));
}
```

- [ ] **Step 2: Run it and watch it fail, then implement the module**

Run: `cd tests/unit && make test_keymap` → fails, header missing. Then write
`include/keymap.h` and `src/keymap.c` and make it pass.

- [ ] **Step 3: Add the field and the three enable paths**

- `include/ssh_server.h`: add `tnt_keymap_t keymap;` to `client_t`.
- `src/main.c`: parse `--keymap default|vim`, storing the server default; reject
  an unknown value with the same error style as the neighbouring flags.
- `src/input.c` session init: `client->keymap = tnt_keymap_from_login(client->ssh_login, g_default_keymap);`
- The session command comes in Task 2 with the rest of the default behaviours.

**The default stays vim in this commit**: `g_default_keymap` is initialised to
`TNT_KEYMAP_VIM`, so nothing a user sees changes yet.

- [ ] **Step 4: Prove nothing changed**

Run: `make test PORT=13830`
Expected: exit 0, and `test_interactive_input.sh` passes unchanged.

- [ ] **Step 5: Commit**

```bash
git commit -m "feat: select a keymap per session"
```

---

### Task 2: What the default keymap does

**Files:** `src/input.c`, `src/tui.c` (mode chip), `src/commands.c` and `src/command_catalog.c` (the `vim` command)

**Interfaces:** consumes `tnt_keymap_uses_modes`.

The default keymap differs from vim in exactly these keys:

| Key | Vim keymap (unchanged) | Default keymap |
| --- | --- | --- |
| `Esc` | enter NORMAL | dismiss a help or command panel; otherwise nothing |
| `Ctrl+C` | enter NORMAL (or exit from NORMAL) | clear the input; when already empty, show how to quit |
| `:` | enter COMMAND | an ordinary character |
| `/` at the start of a line | ordinary character | command prefix; `//` sends a literal `/` |
| `PgUp` / `PgDn` | scroll in NORMAL | scroll the history without leaving the input |
| `Up` / `Down` | recall sent messages | recall only when the input is empty |

- [ ] **Step 1: Write the failing integration test**

`tests/test_default_keymap.sh`, launched with the standard env and readiness
poll, connecting as `ssh anonymous@…` (default keymap) and asserting:

1. typing `:list` and pressing Enter sends the literal text `:list` as a
   message — `:` is not a command prefix here
2. pressing `Esc` then typing `hello` and pressing Enter sends `hello` — Esc did
   not change modes and the following keys still typed
3. `/me waves` runs the action command rather than sending literal text
4. `//slash` sends the literal text `/slash`

- [ ] **Step 2: Gate the mode-entering keys**

In `handle_key`, wrap each mode transition in `tnt_keymap_uses_modes(client->keymap)`
and give the default keymap its own branch, per the table above.

- [ ] **Step 3: Dispatch `/` commands**

On Enter in the default keymap, when the text starts with a single `/` and
`command_catalog_match` recognises the word after it, copy the text without the
leading `/` into `client->command_input` and call `commands_dispatch(client)`.
A leading `//` is sent as a message with one `/` removed. Anything else — a
path like `/usr/local/bin`, or an unknown command — is sent as a message.

- [ ] **Step 4: Status chip**

`src/tui.c` prints `INSERT`/`NORMAL`/`COMMAND`. In the default keymap there is
no mode to report: show the hint chip instead, so nothing on screen asks the
user to understand modes.

- [ ] **Step 5: The session command**

Add `vim` to the command catalog: it switches `client->keymap` for this session
and repaints. It is reachable as `/vim` in the default keymap and `:vim` in vim
mode.

- [ ] **Step 6: Run both suites**

Run: `cd tests && PORT=13840 sh test_default_keymap.sh` → all pass.
Run: `make test PORT=13850` → exit 0, vim behaviour unchanged.

- [ ] **Step 7: Commit**

```bash
git commit -m "feat: give the default keymap its own behaviour"
```

---

### Task 3: Flip the default, and the vim regression net

**Files:** `src/input.c` (`g_default_keymap`), `tests/test_vim_keymap.sh`, `tnt-chat.7`, `tnt.8`, `Makefile`

- [ ] **Step 1: Write the vim regression test first**

`tests/test_vim_keymap.sh` connects as `ssh vim@…` and asserts the current
behaviour key for key: `Esc` enters NORMAL, `i` returns to INSERT, `:` opens
COMMAND, `:list` runs, `j`/`k` scroll. This test must pass **before** the flip,
so it is written against today's behaviour and then proves the flip did not
disturb it.

Run: `cd tests && PORT=13860 sh test_vim_keymap.sh` → passes against the
current default.

- [ ] **Step 2: Flip**

`g_default_keymap = TNT_KEYMAP_DEFAULT;`

- [ ] **Step 3: Both suites again**

Run: `cd tests && PORT=13870 sh test_vim_keymap.sh` → still passes, now via the
`vim@` login rather than the server default. This is the evidence that existing
users keep their keys.
Run: `make test PORT=13880` → exit 0.

- [ ] **Step 4: Documentation**

Rewrite the `tnt-chat(7)` MODES section around the default keymap, with vim as
a clearly marked section listing the three ways to enable it. Document
`--keymap` in `tnt(8)`.

Run: `sh tests/test_manpages.sh` → 14 passed.

- [ ] **Step 5: Release gate, commit, pull request**

Run: `make release-check` → preflight passed.

```bash
git commit -m "feat: make the non-modal keymap the default"
```

---

## Sprint exit criteria

- A first-time user types, scrolls with PgUp/PgDn, and runs `/help` without meeting a mode.
- `ssh vim@host`, `/vim`, and `--keymap vim` each restore the current experience.
- The vim regression test passes both before and after the flip.
- `make test` and `make release-check` green; verified on the live instance.
