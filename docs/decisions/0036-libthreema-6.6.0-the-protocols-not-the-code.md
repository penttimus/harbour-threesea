# 0036 — libthreema 6.6.0: the protocols, not the code

## Context
threema-android 6.6.0-1237 (2026-11-03) brings a libthreema that is halfway through rewriting its
message layer. The wire message types (`IncomingMessageBody`, `ContactMessageBody`, …) moved from
the public `model::message` into `csp_e2e/message/mod.rs` and are `pub(super)` there;
`model::message` is now a storage model (`MessageState`), and the providers were redesigned
(`ContactProvider`, `GroupProvider`, `ConversationProvider` beside their models, a transaction
manager). The new layer handles text, location, delivery receipts, legacy reactions and Web
session resume, the rest is `TODO(LIB-16/53/57)`.

Our commits on libthreema (ADR 0006) sit in exactly those files: our message types (files, media,
polls, edits, deletes, calls, group management), the incoming and outgoing tasks built on them,
and the old providers that `core/src/providers.rs` and `connection.rs` implement and drive. A
trial merge left 17 files in conflict, and beyond them core would have to change over to a design
upstream is still changing.

Nothing on the wire made us follow: the protocol changes are names (fields the protocol now calls
deprecated), comments, and additions that Android 6.6.0 does not use yet (participant UIDs and
moderator approval in group calls, `ConferenceCallInvite`). Android 6.6.0 itself still sends and
reads the old group call and settings fields; it builds them from its own copy of the protocols,
`domain/protocol/src`, which differs from libthreema's only in names and comments.

## Decision
(User decision, 2026-11-15.) libthreema's code stays on 6.5.3-1216 with our commits. From 6.6.0
we take:
- `third_party/libthreema/threema-protocols` as a whole (one `libthreema:` commit, the same files
  as upstream's), and core follows the renamed fields, using them as Android 6.6.0 does;
- the reference submodule `third_party/threema-android` at 6.6.0-1237, for porting Android's
  changes and for the TLS pins (one new domain, `test.threema.com`).

The pristine 6.6.0 tree is on `vendor/libthreema` (recreated at the 6.5.3 import `c108a64`,
which the public history merged) but is not merged. The full merge waits until upstream's message
layer covers what ours does; look again with every upstream tag.

Not chosen: merging and keeping our message layer beside upstream's unfinished one (two message
layers in one library, and every later merge conflicting there again); moving our message
support onto upstream's new design now.

## Consequences
- The vendored code and the reference submodule are on different tags for now; `docs/build.md`
  says which. `scripts/update-libthreema.sh` stays the way to do the full merge later, and since
  the protocol files came over unchanged they merge without conflict then.
- libthreema's toolchain stays 1.95 (6.6.0's wants 1.97), and with it ours (ADR 0002).
- Android's code at 6.6.0 is what `docs/android-mapping.md` refers to from now on; where it uses
  libthreema's new parts (the cross-platform data backup, behind a build flag that is off), we
  have nothing to follow yet.
