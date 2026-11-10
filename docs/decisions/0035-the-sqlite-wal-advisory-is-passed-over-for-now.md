# 0035 — The SQLite WAL advisory is passed over for now

## Context
RUSTSEC-2026-0459 (CVE-2026-119375, 2026-10-30): SQLite before 3.54.0 can return uninitialised
memory of the process when a database is opened together with a crafted `-wal` file of another
page size. We have `libsqlite3-sys` 0.35.0 through `rusqlite` 0.37 with `bundled-sqlcipher`, and
the store runs in WAL mode. Since the advisory `cargo audit` fails, and with it
`scripts/check.sh`, CI on every push and the weekly run (issue 9).

The upgrade `cargo audit` names does not help us: by the advisory's own text, `libsqlite3-sys`
0.39.0 and 0.39.1 bundle the fixed SQLite only without SQLCipher; under `bundled-sqlcipher` they
take `sqlcipher-src` 419.0.6 with SQLite 3.53.4, which is affected. Updating would make the check
pass and leave the fault in.

What it takes to use it against us: the one database the core opens is in the app's own data
folder (`store.rs`), nothing opens a database that came from outside. The files would have to be
put there by something that already runs as the user, and what leaks goes into the app's own
query results, not back to whoever put them there. Whether SQLCipher's page check turns such a
file away first has not been established.

## Decision
The advisory is passed over in `.cargo/audit.toml`, alone and with its reason beside it, so that
`cargo audit` goes on failing for every other advisory (user decision 2026-11-10).

It comes out of that file with the update to the first `libsqlite3-sys` whose `bundled-sqlcipher`
has SQLite 3.54 or newer.

Not chosen: updating to 0.39.x (see above); building SQLCipher from a source patched by us, a
second vendored C library to keep current for a fault that needs local write access to use.

## Consequences
- The checks pass again while the fault is still in the shipped binary: this file and the
  comment in `.cargo/audit.toml` are what remembers it. Look for a fixed release with every
  dependency update and before every release.
- `cargo audit --file third_party/libthreema/Cargo.lock` is untouched: libthreema has no SQLite.
