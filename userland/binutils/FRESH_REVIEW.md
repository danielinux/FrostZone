# `fresh.c` review and proposed refactor

**Date:** 2026-04-25
**Scope:** `userland/binutils/fresh.c` (1599 lines)
**Goal:** catalogue defects, propose a static-allocation Bourne-shell-subset rewrite that adds `if/fi`, `for/while`, `case/esac`, `[` / `test`, full I/O redirection (`>`, `<`, `>>`, `2>`, `>&1`, `>&2`, `<<`), command substitution (`` ` `` and `$()`), `&&` / `||` / `;` operators, and proper job control while keeping the design KISS and statically sized.

---

## Part 1 — defects found by inspection

### Critical / structural

- **D-1: `signalHandler_child` reaps only one child per signal** (`fresh.c:464-471`). Uses `waitpid(-1, ..., WNOHANG)` exactly once. If two children become zombies between handler invocations (e.g., a backgrounded job and a foregrounded one), one zombie leaks. Real shells loop `while (waitpid(-1, &st, WNOHANG) > 0)`.

- **D-2: `launchProg` polls `child_pid` via `sleep(120)` instead of `waitpid(pid, ...)`** (`fresh.c:653-655`). The loop relies on SIGCHLD interrupting `sleep`, which is the source of the recent strace/fresh hangs we've been chasing. Replace with a direct `waitpid(pid, &status, 0)` retried on `EINTR`. Removes the global-`child_pid` race entirely.

- **D-3: Variable expansion runs *after* tokenization, in `commandHandler`** (`fresh.c:906-963`). `parseLine` uses `strtok_r` on whitespace before any `$VAR` is seen, so an expansion can't word-split, can't be quoted, and can't carry whitespace. Expansion belongs *between* lex and parse, after quote removal.

- **D-4: Variable expansion leaks memory** (`fresh.c:958-963`). Allocates a new buffer for the expanded value and writes it into `args[j]`. The original `args[j]` was a pointer into the line buffer, so we don't lose it; but the *malloc'd* replacement is never freed. Each `$VAR` use leaks `strlen(value)+1` bytes per command.

- **D-5: Stray `done:` label with no `goto`** (`fresh.c:177`). Dead label; harmless today, but it's a clear leftover from refactoring and trips `-Wunused-label` if ever enabled.

- **D-6: `pipeHandler` does redirection in the parent before vfork** (`fresh.c:823-825`). The comment acknowledges the pattern. This works only because vfork's child runs synchronously to its `exec`; any future change to fork semantics would corrupt the parent's stdio. The clean shape is "redirect after vfork in the child, before exec" or build a child-side helper.

- **D-7: Pipelines don't combine with redirection** (`fresh.c:1136-1141`). On `cmd1 | cmd2 > file` the parser sees `|` first, dispatches to `pipeHandler`, and the trailing `> file` is dropped. Likewise `cmd > a | cmd2`. The redirection logic and the pipeline logic are disjoint.

- **D-8: `bin_arg0[60]`** (`fresh.c:586`) is too small. Caps absolute paths at 59 chars. Scripts under `/mnt/...` can be longer; the tail is truncated and `stat` fails with a confusing "Command not found".

- **D-9: `commandHandler` mutates `args[]` in place** (`fresh.c:894-905, 1132`). It both reads and writes the same array while iterating with `j`/`i`, copies to `args_aux[]` only for tokens before any operator, and conditionally NULLs slots based on `&` detection. Hard to audit, easy to break. The flow needs to be replaced with a parsed AST.

### Behavioural / POSIX deviations

- **D-10: No quoting**. `'`, `"`, and `\\` are not recognised. `echo "hello world"` is two separate tokens.

- **D-11: No `&&` / `||` / `;`**. The only operator-level chaining is `|`.

- **D-12: No multi-line constructs**. `if/fi`, `for`, `while`, `case` aren't parsed at all.

- **D-13: No `test` / `[` builtin.**

- **D-14: No fd duplication redirects**. `>&1`, `>&2`, `2>&1` aren't recognised. Only `>`, `>>`, `<`, `2>` (each on its own).

- **D-15: No command substitution**. `` ` `` and `$()` are not parsed.

- **D-16: `${VAR}` curly form not supported** — only the bare `$VAR`.

- **D-17: `cd` doesn't handle `~`, `~user`, or `cd -`** (`fresh.c:502-519`).

- **D-18: `pwd > file` is a special-cased builtin redirect** (`fresh.c:996-1009`), inconsistent with the general redir machinery, and only handles `>` (not `>>`/`2>`/etc.).

- **D-19: Background launch always prints `Process created with PID: N\\r\\n`** (`fresh.c:664`). Real shells emit `[1] N` only when job control is enabled and only on the *job* level.

- **D-20: Pipeline exit status reports last stage only**. POSIX `set -o pipefail` and `${PIPESTATUS[@]}` are out of scope, but at minimum the `?` env should reflect the last stage which it does — fine, but worth noting.

- **D-21: SIGINT handler increments `interrupted`** (`fresh.c:493-497`) and nothing reads it. The variable is dead state.

- **D-22: `tokens[LIMIT]` cap of 16** (`fresh.c:60`) is small. With proper word splitting on `$IFS` you'll routinely exceed it.

- **D-23: `parseLine` strips inline `#` comments** (`fresh.c:1453-1455`) but doesn't honour `\\#` or `'#'` inside strings. Not relevant today (no quoting), will matter post-refactor.

- **D-24: `redir_setup` mode 1 (`<` + `>`) is special-cased** (`fresh.c:687-704`) instead of being two independent operations. Adding `2>` to the same command requires another bespoke mode.

### Style / robustness

- **D-25: `while (2 > 1)` infinite loop** (`fresh.c:1231`). Replace with `for (;;)`.

- **D-26: `pointer_shift` is unused** (`fresh.c:1221-1227`).

- **D-27: `puts_r` is a no-op stub** (`fresh.c:92-95`) returning `strlen(s)` without writing. Either implement or delete.

- **D-28: `welcomeScreen` no longer prefixed `static`** — leaks into the `icebox` link.

- **D-29: `shell_init` busy-loops 100x on `open(/dev/ttyS0)`** (`fresh.c:414-418`) without any backoff. Burns CPU briefly but otherwise harmless.

- **D-30: Inconsistent return convention** in `commandHandler` (-1, 1, 0, -ENOMEM, -errno, exit codes). Standardise on "0..255 = exit status, <0 only on internal error before launch".

- **D-31: `signalHandler_int` writes `interrupted++`** but doesn't `volatile sig_atomic_t`-promote the type. `volatile static int interrupted` is the closest, fine for a single-core no-preempt-during-handler model but not portable.

- **D-32: `history_load` is commented out** (`fresh.c:236`) — `history_init` only sets the path. Persistence loaded on next session is dead code today.

- **D-33: `lastcmd` is captured but never read.** The `!!` history expansion you'd expect from a shell isn't implemented; the bookkeeping is dead.

---

## Part 2 — proposed refactor

### Goals

1. **Static memory** — no malloc in the hot path. Token, AST, and argv pools sized at compile time. The only dynamic allocation acceptable: capture buffer for `$()`/`` ` `` output, with a hard cap.
2. **Pipeline of clear stages** — lex → expand → parse → exec. Each stage has a well-defined input and output type.
3. **KISS Bourne subset** — no aliases, no functions, no arrays, no parameter expansion fancy forms (`${var:-x}`, `${var#prefix}`, etc.), no arithmetic. Add later if needed.
4. **POSIX-shaped semantics** for the things that *are* supported.

### Memory budget (suggested)

```c
#define MAX_LINE_BYTES   1024  /* one logical line (after continuation) */
#define MAX_WORDS         64   /* tokens per parsed line              */
#define MAX_WORD_BYTES    96   /* per word, after expansion           */
#define MAX_AST_NODES     32   /* per top-level command               */
#define MAX_REDIRS         8   /* per simple command                  */
#define MAX_PIPELINE       8   /* stages per |-pipeline               */
#define MAX_NESTING        4   /* if/for/while depth                  */
#define CMDSUB_BUF_BYTES 512   /* captured stdout from $()            */
```

Total static footprint ≈ **18 KB** in BSS (`MAX_WORDS * MAX_WORD_BYTES + AST + line + cmdsub`). Fine for our targets.

### Five-layer architecture

```
   +---------------------+
   |  reader  (tty/file) |   line-at-a-time, with backslash-newline
   +----------+----------+   continuation and unterminated-quote
              v              continuation
   +----------+----------+
   |       lexer         |   produces tokens with type + lexeme
   +----------+----------+   (no expansion, no quote removal yet)
              v
   +----------+----------+
   |       parser        |   recursive descent → AST in a node pool
   +----------+----------+
              v
   +----------+----------+
   |     expander        |   walks AST, expands $VAR, $(), `` ` ``, ~,
   +----------+----------+   does word splitting on IFS, quote removal
              v
   +----------+----------+
   |      executor       |   forks/execs, wires pipes/redirs, runs
   +---------------------+   builtins, evaluates AST nodes
```

### Token types (lexer output)

```c
enum tok {
    TOK_WORD, TOK_ASSIGN,                    /* foo=bar               */
    TOK_PIPE, TOK_AMP, TOK_SEMI, TOK_AMPAMP, TOK_PIPEPIPE,
    TOK_LT, TOK_GT, TOK_DGT,                 /* < > >>                */
    TOK_LTLT,                                /* <<  (heredoc later)   */
    TOK_GTAMP, TOK_NUMGT,                    /* >&N , N>              */
    TOK_LP, TOK_RP, TOK_LBRACE, TOK_RBRACE,
    TOK_BACKQUOTE,                           /* ` ... `               */
    TOK_DOLLARLP,                            /* $(                    */
    TOK_NEWLINE, TOK_EOF,
    /* reserved words: */
    TOK_IF, TOK_THEN, TOK_ELSE, TOK_ELIF, TOK_FI,
    TOK_FOR, TOK_DO, TOK_DONE, TOK_IN,
    TOK_WHILE, TOK_UNTIL,
    TOK_CASE, TOK_ESAC,
    TOK_LBRACK, TOK_RBRACK,                  /* [ and ]               */
    TOK_NOT,                                 /* leading ! for negate  */
};
```

Reserved-word recognition is **position-sensitive**: only at the start of a command does `if` get TOK_IF; elsewhere it's a TOK_WORD. Handled in the parser, not the lexer.

### AST node shapes

```c
struct ast {
    uint8_t  type;        /* CMD, PIPE, AND_OR, SEQ, IF, FOR, WHILE, CASE, SUBSH */
    uint8_t  flags;       /* AND, OR, BACKGROUND, NEGATE                         */
    uint16_t left, right; /* indices into nodes[]; 0xFFFF = none                 */
    union {
        struct { uint16_t argv_first, argv_n;
                 uint16_t redir_first, redir_n; }    cmd;
        struct { uint16_t cond, then_, else_; }     ifn;
        struct { uint16_t name; uint16_t list_first, list_n; uint16_t body; } forn;
        struct { uint16_t cond, body; }              loopn;
        struct { uint16_t word; uint16_t case_first, case_n; } casen;
        struct { uint16_t body; }                    subsh;
    };
};
```

Indices (not pointers) into a single `struct ast nodes[MAX_AST_NODES]` so the whole tree is one contiguous block — easy to dump in a debugger, no fragmentation.

### Redirection record

```c
struct redir {
    uint8_t op;     /* R_IN, R_OUT, R_APPEND, R_ERR, R_DUP, R_HEREDOC */
    int8_t  fd;     /* fd being redirected (default 0 for <, 1 for >) */
    int8_t  dup_fd; /* for R_DUP: the source fd                       */
    uint16_t word;  /* index of the target filename word (or NULL)    */
};
```

`>&1` → `R_DUP, fd=2, dup_fd=1`. `2>&1` → same. `> file` → `R_OUT, fd=1, word=...`.

### Expansion pass

A single function `expand_word(struct ast *root, uint16_t word_idx, char *out, size_t out_sz, char ***argv_out, int *argc_out)` that walks the word's segments (literal text, `$VAR`, `${VAR}`, `$()`, `` ` ``, `~`) and produces one or more output words via word-splitting on `$IFS`. For quoted segments, no splitting and no glob.

`$()` / `` ` `` implementation: classic
```
pipe(fds);
pid = vfork();
if (pid == 0) {
    dup2(fds[1], 1);
    close(fds[0]);
    /* recursively run the captured command tree as a subshell */
    exec_ast(captured_root);
    _exit(last_status);
}
close(fds[1]);
read until EOF into CMDSUB_BUF_BYTES, strip trailing newlines.
waitpid(pid, ...);
```

### Executor sketch

```c
static int exec_ast(struct ast *root, uint16_t idx)
{
    struct ast *n = &nodes[idx];
    switch (n->type) {
    case AST_SEQ:
        exec_ast(root, n->left);
        return exec_ast(root, n->right);
    case AST_AND_OR: {
        int s = exec_ast(root, n->left);
        if ((n->flags & AND) && s != 0) return s;
        if ((n->flags & OR)  && s == 0) return s;
        return exec_ast(root, n->right);
    }
    case AST_PIPE:        return run_pipeline(root, idx);
    case AST_IF:          return run_if(root, idx);
    case AST_FOR:         return run_for(root, idx);
    case AST_WHILE:       return run_while(root, idx);
    case AST_CASE:        return run_case(root, idx);
    case AST_SUBSH:       return run_subshell(root, idx);
    case AST_CMD:         return run_simple(root, idx);
    }
    return 1;
}
```

Each construct is ~30 lines. `run_simple` does: expand argv, apply redirections, dispatch to builtin or `vfork`+`exec`, restore.

### Builtins to add

- `:` (true), `true`, `false`
- `[`/`test`: `-e`, `-f`, `-d`, `-r`, `-w`, `-x`, `-s`, `-z s`, `-n s`, `s = s`, `s != s`, `n -eq n`, `-ne`, `-lt`, `-le`, `-gt`, `-ge`, plus `!`, `-a`, `-o`. Reject `[[`/`]]` (too much for KISS).
- `read VAR [VAR...]`
- `return N` (only inside sourced files)
- `set -e`, `set -x` flags via a single bitmap.

### Reader: how multi-line constructs come in interactively

The reader needs to know when a line is "incomplete" (`if` opened, `for` opened, unterminated quote, `&&`/`||` at end). The trick: a tiny **continuation predicate** state machine that tracks open-brace depth. The interactive prompt switches from `# ` to `> ` while open. This keeps the parser monoline-safe (it always gets a complete logical command).

Alternative even simpler: parse incrementally, on parse-error retry with an extra read. KISS-er.

### Static guarantees

- **Zero malloc** during lex and parse.
- **One malloc per `$()`** in the worst case (`CMDSUB_BUF_BYTES` could just be a static scratch buffer if we serialise substitutions, since they fork+wait anyway).
- **Re-entrant on `source`**: stack the reader state, AST pool, and word storage. Use a stack-of-pools indexed by current source depth.

### What stays

- Tokenizer-only `#` comment handling (existing logic).
- `history_*` (already static and bounded).
- `shell_init`, `shellPrompt`, `update_pwd_env`, signal-handler scaffolding.
- `get_x_type`, `resolve_cmd`, `redir_setup`/`redir_restore` (used as primitives by the new executor, not as the parser entry point).
- `pipeHandler`'s "create all pipes, fork all stages, close all pipe fds in parent, waitpid each" pattern — wrap it in `run_pipeline(ast)` so it's reused for any pipeline shape, including `cmd | (subshell) | cmd > file`.

### What goes

- The current `commandHandler` (replaced wholesale by `exec_ast` + builtins).
- The in-place `$VAR` mutation in `commandHandler`.
- `pointer_shift`, `puts_r`, `interrupted` global, the `done:` label, `lastcmd`.
- Special-cased `pwd > file` (handled by general redirection now).
- The `child_pid`/`sleep(120)` dance in `launchProg` (replaced by direct `waitpid`).

---

## Part 3 — phasing (suggested merge order)

Each phase ends with a buildable, testable shell. None of them break the existing init.sh.

1. **P0 — defect cleanup** (~1 commit). Fix D-1 (loop in SIGCHLD handler), D-2 (replace `sleep(120)` with `waitpid`), D-4 (memory leak), D-5 (`done:` label), D-25 / D-26 / D-27 (style/dead code), D-8 (size `bin_arg0` to `PATH_MAX`). Pure improvements, no new features.
2. **P1 — lexer + word-storage pool**. Introduce the tokenizer, run it inside the existing `parseLine` so the rest of the code still consumes a `char *tokens[]` array, just now produced by the new lexer. Quotes, `\\` escape, and `${VAR}` start working. Deliverable: existing scripts still run; `echo "hello world"` produces one token.
3. **P2 — expander + `$()` / `` ` ``**. Move variable expansion out of `commandHandler`. Add command substitution with a static capture buffer.
4. **P3 — operators (`&&`, `||`, `;`, negation `!`)**. Tiny AST (only AND_OR and SEQ nodes); `commandHandler` becomes the simple-command leaf executor.
5. **P4 — pipelines as AST**. Fold the existing `pipeHandler` into `run_pipeline(ast)`. Combine pipelines with redirections cleanly.
6. **P5 — `if/fi`, `while/until`, `for`, `case/esac`**. Add reserved-word handling and the corresponding AST nodes/runners. Multi-line continuation in interactive mode.
7. **P6 — `[` / `test` builtin** + `read`, `return`, `set -e/-x`.

After P6 you have a usable POSIX-flavoured shell in roughly 1500–1800 lines, statically sized, with zero malloc on the hot path.

---

## Part 4 — testable correctness checklist

Tests to land alongside the refactor (run via the existing `init.sh` smoke harness or a new `test-fresh.sh` in `userland/sh/`):

```sh
# quoting / expansion
[ "$(echo a b c | wc -w)" = "3" ] || exit 1
A="hello world"
[ "$A" = "hello world" ] || exit 1     # no word-split when quoted
set -- a b c
[ "$#" = "3" ] || exit 1
[ "$2" = "b" ] || exit 1

# redirection
echo hi > /tmp/x && [ "$(cat /tmp/x)" = "hi" ] || exit 1
echo y >> /tmp/x && [ "$(wc -l < /tmp/x)" = "2" ] || exit 1
ls /no/such 2> /tmp/e
[ -s /tmp/e ] || exit 1
ls /no/such > /tmp/o 2>&1
grep -q "No such" /tmp/o || exit 1

# operators
true && echo ok | grep -q ok || exit 1
false || echo fall | grep -q fall || exit 1
true; false; echo $?    # expect 1

# pipelines
seq 1 5 | wc -l | grep -q 5 || exit 1

# if / for / while
i=0
for x in a b c ; do i=$((i+1)) ; done
[ "$i" = "3" ] || exit 1   # arithmetic out of scope; replace with three separate calls if no $((..))

if [ -d / ] ; then echo dir ; else echo nope ; fi | grep -q dir || exit 1

# command substitution
NOW="$(date +%s)"
[ -n "$NOW" ] || exit 1

# subshell
( cd /tmp && pwd ) | grep -q "^/tmp$" || exit 1
[ "$(pwd)" != "/tmp" ] || exit 1     # outer shell unchanged
```

The test script itself becomes the regression harness — no host-side runner required.
