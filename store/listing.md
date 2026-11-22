# Ignition: appstore listing

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
- Release notes: First release.
- Platforms in the build: Pebble Round 2 (gabbro) and Pebble Time Round (chalk).

## Asset collections

Create one for **gabbro** and one for **chalk**, each with the description below
and that platform's screenshots from `store/screenshots/`.

### Description (both platforms, max 1600 characters)

33 engines away from making life multiplanetary with this stellar watchface. Moonphase and mars transit complications, launch countdowns, and much more.

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
