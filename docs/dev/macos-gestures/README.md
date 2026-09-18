# macOS synthetic swipe gestures — reference spike

Reference code for synthesizing multi-finger swipe gestures on macOS, so a Deskflow
client can reproduce Spaces / Mission Control navigation driven from a remote trackpad.

**This is not part of the build.** It is a standalone, verified reference kept in-tree
because re-deriving the constants is the expensive part of the work.

## Status

Verified on **macOS 27 (Darwin 27.0.0), Apple silicon, 2026-09-18**. Requires only
Accessibility permission — no SIP disable, no code injection, no driver or system extension.

| Gesture | Observable used | Result |
|---|---|---|
| 3-finger up | Dock on-screen window count 0 → 1 | 3/3 opened Mission Control |
| 3-finger left / right | `CGSGetActiveSpace` changed and returned | 3/3 clean round trips |

Measured programmatically rather than visually.

## Files

- `swipe_spike.c` — posts a synthetic horizontal swipe. `swipe_spike left|right`.
  Change motion to `2` and move position/velocity to the Y axis for a vertical swipe.
- `spaces.c` — counts Spaces per display. Horizontal swipes cannot be tested with only
  one Space; a correct swipe looks like a failure.
- `mcstate.c` — counts Dock-owned on-screen windows, used to detect Mission Control.

Build (each is standalone):

    clang -O2 -o swipe_spike swipe_spike.c -framework ApplicationServices -framework CoreFoundation
    clang -O2 -o spaces spaces.c -framework CoreFoundation
    clang -O2 -o mcstate mcstate.c -framework CoreGraphics -framework CoreFoundation

## How it works

Swipes that drive Spaces and Mission Control are **not** `kCGEventGesture`. They travel
over a private **DockControl** event path owned by WindowServer.

- Event types: DockControl = `30`, companion gesture = `29`; IOHID DockSwipe = `23`
- Events are posted in **pairs**: the DockControl event, then a bare companion event,
  both to `kCGSessionEventTap`
- Phase sequence `began(1) → changed(2) → ended(4)`, roughly 16 ms apart
- Motion field: `1` = horizontal, `2` = vertical

### macOS 27 requirement

Before macOS 27, setting the CGEvent fields was sufficient. macOS 27's WindowServer
rejects those events. They must additionally carry a serialized raw IOHID queue payload
appended to CGEvent field **4205**: a queue header plus a fluid-touch gesture record,
plus a velocity record when velocity is non-zero or the phase is `ended`.

Struct sizes (16 / 40 / 28 / 28) are asserted at compile time via `_Static_assert`, so a
layout change on a future OS fails the build rather than silently misbehaving.

## Known limits

- **Finger count is not encoded.** Nothing in these events distinguishes a 3-finger from a
  4-finger swipe; macOS maps both onto the same dock swipe action. Any protocol carrying
  these should send direction and phase, not a finger count.
- `CGSGetActiveSpace` and `CGSCopyManagedDisplaySpaces` resolve only from the dyld shared
  cache. Static linking against SkyLight fails; use `dlopen`/`dlsym` as `spaces.c` does.
- `CGSGetActiveSpace` can lag behind the Dock right after a synthetic switch. Allow a short
  settle before reading it back.

## Provenance and stability

Constants, struct layouts and the posting sequence are derived from
[joshuarli/iss](https://github.com/joshuarli/iss) (0BSD), re-verified independently here.
A related approach using SkyLight's `SLEventSetIOHIDEvent` is in
[channelramble/MouseDragFix](https://github.com/channelramble/MouseDragFix).

These are undocumented implementation details. Apple broke this path once already, at
macOS 27. Expect it to need re-derivation on future releases, and keep the constants
isolated so that when it breaks, the blast radius is one file.
