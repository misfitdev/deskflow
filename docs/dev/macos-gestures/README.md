# macOS trackpad swipe gestures

Reference code and findings behind Deskflow's trackpad swipe forwarding (protocol 1.9),
which lets a remote Mac trackpad switch Spaces and open Mission Control on a client.

The shipping implementation is `src/lib/platform/OSXSwipe.{h,cpp}`, wired into
`OSXScreen`, with tests in `src/unittests/platform/OSXSwipeTests.cpp`. The files here
are **not part of the build**: they are standalone tools for re-verifying the private
event format when a macOS release breaks it.

## Status

Verified on **macOS 27 (Darwin 27.0.0), Apple silicon, 2026-09-18**. Requires only
Accessibility permission — no SIP disable, no code injection, no driver or system extension.

| Gesture | Observable used | Result |
|---|---|---|
| 3-finger up | Dock on-screen window count 0 → 1 | 3/3 opened Mission Control |
| 3-finger left / right | `CGSGetActiveSpace` changed and returned | 3/3 clean round trips |

Measured programmatically rather than visually.

Live end-to-end, using `replay_test` against the real `OSXSwipe.cpp` (2026-09-22):
17 real swipes, each decoded exactly once and replayed as the same navigation.
Up opened Mission Control, down closed it, and right switched to the next Space.

## Capture

- Deskflow's existing `kCGHIDEventTap` sees DockControl events. A listen-only probe saw
  identical streams at the HID and session taps, so no extra tap is needed.
- A real swipe is `began`, then a run of `changed` events with progress growing
  toward ±1, then `ended` with a velocity of the same sign as the progress.
- The detector fires on the first non-zero progress, or on the ending velocity for a
  flick that reports no progress.

## Files

- `swipe_spike.c` — posts a synthetic horizontal swipe. `swipe_spike left|right`.
  Change motion to `2` and move position/velocity to the Y axis for a vertical swipe.
- `spaces.c` — counts Spaces per display. Horizontal swipes cannot be tested with only
  one Space; a correct swipe looks like a failure.
- `mcstate.c` — counts Dock-owned on-screen windows, used to detect Mission Control.
- `replay_test.cpp` — end-to-end check of `OSXSwipe.cpp`. It swallows each real swipe
  and replays it through the detector and synthesizer, printing what changed. If
  swipes still behave normally, capture and synthesis agree with macOS.

Build (each is standalone):

    clang -O2 -o swipe_spike swipe_spike.c -framework ApplicationServices -framework CoreFoundation
    clang -O2 -o spaces spaces.c -framework CoreFoundation
    clang -O2 -o mcstate mcstate.c -framework CoreGraphics -framework CoreFoundation

`replay_test` builds from the repository root because it links the real implementation:

    clang++ -std=c++20 -O2 -Isrc/lib -o replay_test \
      docs/dev/macos-gestures/replay_test.cpp src/lib/platform/OSXSwipe.cpp \
      -framework ApplicationServices -framework CoreFoundation

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
  4-finger swipe; macOS maps both onto the same dock swipe action. The protocol therefore
  sends only a direction.
- **Swipes are discrete.** The client replays a complete swipe as soon as the server
  knows its direction, so the remote Space switches instantly, without following the
  fingers.
- **Server on macOS 27 or later.** Swipe recognition is only verified against macOS 27
  event streams, so older servers do not forward swipes.
- **Clients before macOS 27 use the legacy layout** from iss: no IOHID payload, direction
  in field 135 and a ±400 ending velocity, with the opposite sign to macOS 27 for the
  same navigation. iss only covers horizontal swipes, so the vertical sign (Mission
  Control) is inferred by applying the same flip and must be confirmed on a real
  macOS 26 client. If up and down come out swapped there, only vertical needs flipping
  in `legacyDirectionSign`.
- The build targets macOS 26 (`macos_min` in the justfile). Homebrew's OpenSSL, linked
  statically, sets that floor.
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
