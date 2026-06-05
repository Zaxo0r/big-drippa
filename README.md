# Big Drippa 💧

An ESP32-based **drip-irrigation controller** that keeps a small balcony garden alive during
multi-day absences. One watering zone feeds every plant at once; per-plant flow is tuned with
adjustable drippers rather than separate circuits.

The garden: one *pallkrage* raised bed (~77×58 cm) with jalapeño, broccoli, and grönkål, plus
several buckets of jalapeños, potatoes and tomatoes — outdoors, in Sweden.

## Repository layout

This is a monorepo; each component lives in its own folder.

| Folder | What | Status |
|--------|------|--------|
| [`irrigation/`](irrigation/) | ESP32 firmware (Arduino sketch) | **Phase 1 — active** |
| `web/` *(later)* | Static web app for remote monitoring/control | Phase 2 — not created yet |

## Phases

**Phase 1 — local only (current).** The ESP32 waters on a fixed schedule with no network
dependency. A **MOS hardware timer wired in series with the pump** is an independent safety
cutoff that fires even if the firmware hangs.

**Phase 2 — remote (future).** WiFi + Firebase Realtime Database (Google Sign-In, per-UID
rules) with two channels — `/commands` (web → ESP32) and `/history` (ESP32 → telemetry) — and
a static web app on Firebase Hosting. The web app will live in a sibling `web/` folder in this
same repo.

## Getting started

The firmware, with full hardware/wiring/flash instructions, is in
**[`irrigation/`](irrigation/)** — see [`irrigation/README.md`](irrigation/README.md).
