# Ignition: appstore listing

**Published:** version 1.0.0, live at https://apps.rebble.io/en_US/application/6b025987bb43f20009804f0a
(also in the Pebble app's store, which shares Rebble's backend). Publish updates
with **New Release** on the app's page in the portal, with a higher version number.

Fill these into the Rebble Developer Portal (https://dev-portal.rebble.io) under
**Add a Watchface**. A listing published there also appears in the Pebble app's
store (Core Devices).

## Basic info

| Field | Value |
| --- | --- |
| Title | Ignition |
| Type | Watchface |
| Source code URL | https://github.com/skuthus/pebble-ignition |
| Support email | (your account email; the portal fills it in) |
| Website | (the site we build next) |

## Release

- File: `build/ignition-face.pbw` (version 1.0.0)
- Release notes:
  ```
  First release.
  • 33 engines tell the time: AM/PM, hours and minutes, with ignition and shutdown animations.
  • 8 rim complications: Month, Hours, Steps, Day timeline, T-minus, Moon phase, Moon & Mars distance, Seconds.
  • Flick or tap to switch complications and see the date.
  • For Pebble Round 2 and Pebble Time Round.
  ```
- Platforms in the build: Pebble Round 2 (gabbro) and Pebble Time Round (chalk).

## Asset collections

Create one for **gabbro** and one for **chalk**, each with the description below
and that platform's screenshots from `store/screenshots/`.

### Description (both platforms, max 1600 characters)

```
33 engines away from making life multiplanetary with this stellar watch face. Moon phase and Mars transit complications, launch countdowns, and much more.

---

HOW TO READ IT
• AM/PM: the top center engine is lit in the morning and dark after noon.
• Hours: the other 12 engines inside the thin ring. Count the lit ones. They fill clockwise around the inner ring (a full ring is 10 o'clock), and the two lower center engines add 11 and 12.
• Minutes: the outer ring of 20 engines, one for every 3 minutes, filling clockwise from the top. The white-hot engine is the newest.
• Rim: the outer track shows one complication. Filled dots have passed, the circled dot is now. By default it shows the month, with today circled.

COMPLICATIONS
Month, Hours, Steps, Day timeline (sunrise, solar noon, sunset), T-minus countdown, Moon phase, Moon & Mars distance, and Seconds.

FLICK OR TAP
Flick your wrist, or tap the screen on Pebble Round 2, to cycle through complications and show the day and date. It returns after a few seconds. Optionally the engines rearrange too.

SETTINGS
Choose a clock-like dial or a startup-sequence engine order, flame colour, step goal, countdown date, which complications to show, an optional pulsing minute, and more.
```

### Screenshots (up to 5 per platform, PNG or GIF)

| # | gabbro (260x260) | chalk (180x180) | Shows |
| --- | --- | --- | --- |
| 1 | `gabbro-1-ignition.gif` | `chalk-1-ignition.gif` | Startup ignition sequence |
| 2 | `gabbro-2-time.png` | `chalk-2-time.png` | The engines telling the time, Month rim |
| 3 | `gabbro-3-moon.png` | `chalk-3-moon.png` | Moon phase complication with the date readout |
| 4 | `gabbro-4-mars.png` | `chalk-4-mars.png` | Moon & Mars distance complication |
| 5 | `gabbro-5-countdown.png` | `chalk-5-countdown.png` | T-minus countdown, methane flame |

Banner: optional, not made yet.

## Before publishing

- Build the release with the version that will ship (1.0.0) and upload that `.pbw`.
- Each later update must have a higher version number and keep the same UUID.
- Try **Publish Privately** first if you want to check the listing; a public
  listing can't be made private again.
