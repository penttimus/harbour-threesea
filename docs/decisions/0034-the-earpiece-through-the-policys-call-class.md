# 0034 — The earpiece through the audio policy's call class

## Context
Calls and voice messages at the ear came from the loudspeaker on a phone (issues 8 and 10). Our
streams carry PulseAudio's role `phone`, and the loudspeaker button used the route manager's
`speaker` feature (ADR 0012). Neither opens the earpiece:

- No rule of the policy's stream classification (`xpolicy.conf`) looks at the role.
- The route rule (policy-settings-common, `audio_route_rules.pl`) reads "do not route to earpiece
  unless there is a call or the route is preferred": the earpiece is valid only while a resource
  set of the class `call` has audio playback, or while it is the preferred device.
- The `speaker` feature is only allowed during a phone call (the modem's), for App Support's
  class `aliencall`, or for an emergency call or the radio (`policy.dres`).

An ordinary app can have the class: ohm's resource plugin would check the credential `Cellular`
for it, but is built without the credentials library and accepts. Sailjail's Audio permission
(part of Microphone) covers the resource manager and the route manager. Jolla's own call manager
asks for the same set (`voicecall`, `audiocallpolicyproxy.cpp`), and the client tool describes the
class as "for native or 3rd party telephony".

Tried on a phone on 2026-11-10 (a test build, and `resourceqt5-client` before it): with the class
held the route became `earpiece`, the sink's port `output-earpiece`, the microphone's port stayed
the built-in one (the policy calls the input `voicecallrecord` meanwhile), a voice message and a
call were heard at the ear and the other side heard us. In the emulator a process that is killed
loses the class and the route goes back.

## Decision
While a call or a voice message at the ear runs, the app holds a resource set of the class `call`
with audio playback (our process's streams) and recording: `bridge/callclass.h`, through
`libresourceqt5`. Full only, like everything that uses it.

- **Calls** hold it from the moment they leave "incoming" to their end.
- **Voice at the ear** holds it while the earpiece's pipeline exists, and starts playing only
  once the route has left the loudspeaker. If it doesn't within 1.5 s (no earpiece, the class
  refused or taken by a phone call), the message goes on through the loudspeaker's player as
  before, not at call volume beside the ear.
- **The loudspeaker button** (`bridge/callroute.h`) makes the loudspeaker the route manager's
  preferred device (`Prefer("speaker", output, 1)`) and takes that back for the earpiece. In the
  policy this is what the `speaker` feature does too (`set_preferred_audio_entry`), without the
  feature's gate. The button shows while the sound is on the earpiece or on the loudspeaker by
  our doing, and is lit only once the route itself says so. Only the button moves a call: a
  camera coming on no longer does (user decision 2026-11-10: no automatism).
- The streams keep the role `phone`: that is what was tested.

New dependency: `libresourceqt5` (LGPL, Jolla's client library of the resource policy, installed
on every device; linked dynamically, Full only).

Not chosen:
- `Prefer("earpiece")` for the earpiece itself. It needs no library, but a preference stays after
  the process has gone; another Sailfish client (piirit) took it out for that. The class goes
  with the process.
- Setting the sink's port over PulseAudio (what Yottagram does): the policy doesn't know and
  sets its own port again at its next decision; sink names differ per device.
- Leaving the class for the loudspeaker instead of a preference: crash-safe, but the call would
  stop being one for the system (music could start over it) and its streams would change group
  and volume in the middle.

## Consequences
- While the class is held the system takes it for a call: other apps' music pauses, also for a
  voice message at the ear, and doesn't start again by itself afterwards.
- The policy puts the audio card into its call profile without a call on the modem. Fine on the
  phone it was tried on; another device's audio HAL may differ (device checklist).
- The loudspeaker preference would outlive a crash during a call on the loudspeaker: every sound
  stays on the loudspeaker, also with a headset, until the next phone call. It is taken back at
  the call's end, when the app closes, and at the next start after a crash (a mark in the
  settings, `CallRoute::recover`; Full starts with the session).
- A headset connected while the loudspeaker is preferred doesn't get the sound until the button
  is turned off.
- The volume keys change the media volume, and our playback follows it (user decision
  2026-11-10: one volume, the media's). The system does neither by itself: it ties the keys to
  a call's volume only while the policy's own call state is active, which the modem or App
  Support set; during ours they changed the ringtone's volume (to the system no media plays),
  and our streams (group `call`, 100 %) don't follow the media volume. So while the class is
  held, `bridge/callvolume.h`
  - asks the policy for the volume keys while the app is in front (the `ScaleButton` resource,
    as a camera or a player does), which hands the presses to our window, and moves the media
    volume's step over PulseAudio's `MainVolume2` interface (the system's volume bar uses it;
    its socket is in the folder the Audio permission shows); on a headset not above the step
    the system warns at;
  - turns the step, whoever moved it, into a gain element in our playback pipelines: the top
    step as the sound comes, the first 24 dB below it, zero silent. Not the stream's own volume:
    PulseAudio stores that under the role the system's phone calls share.

  Zero is respected (user decision 2026-11-10; first built with a quiet minimum for calls): the
  app doesn't decide that something is to be heard. A notice in the middle of the screen says
  "Turn the volume up to hear the call" (or "the message"), as WhatsApp does, when a call or a
  message at the ear runs at zero, and again when the phone comes away from the ear.
  A call starts at the media volume's level, and the keys leave the media volume changed after
  it. No volume bar shows. Behind another app the keys stay the system's.
- Lite is untouched: no calls, no voice at the ear, no library.
