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
| [`irrigation/`](irrigation/) | ESP32 firmware (Arduino sketch) | **Phase 2 — active** |
| [`web/`](web/) | Static web app for remote control | **Phase 2 — active** |

## How it works

The ESP32 connects to WiFi and polls Firebase Realtime Database for a manual-run command. A
static web app in [`web/`](web/) lets you trigger the pump remotely via Google Sign-In. A **MOS
hardware timer wired in series with the pump** is an independent safety cutoff that fires even
if the firmware hangs.

## Getting started

The firmware, with full hardware/wiring/flash instructions, is in
**[`irrigation/`](irrigation/)** — see [`irrigation/README.md`](irrigation/README.md).
