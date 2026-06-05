# Wiring — Big Drippa

> **Placeholder.** Fill in the real pin numbers and add a photo/diagram once the hardware is
> wired and verified.

## Pin table

| Signal | ESP32 GPIO | Connects to | Notes |
|--------|-----------|-------------|-------|
| Pump relay | `GPIO 26` | Relay module IN | High-level trigger (HIGH = pump on) |
| Status LED | `GPIO 2` | Onboard LED | Lit while the pump runs |
| GND | `GND` | Relay GND / common ground | Share ground with the relay logic side |

(Keep this in sync with `config.h`.)

## Power path

```
12 V / 3 A adapter --> barrel-jack screw terminal --> [ MOS timer ] --> [ relay ] --> 12 V pump
```

- The **MOS hardware timer** and the **relay** are in **series** with the pump, so either can
  cut pump power independently. The MOS timer is the failsafe; the relay is firmware control.
- The ESP32 is powered separately (USB during Phase 1).
- Keep grounds common where the relay's logic side references the ESP32.

## Notes

- Relay is a **high-level trigger**: pump runs when the control pin is HIGH. Firmware drives
  the pin LOW at boot so the pump never pulses on power-up.
- Pump sits dry on the reservoir edge; suction tube into the water.
- Drippers must sit **above the water line** to prevent siphoning when the pump is off.

## To document later

- [ ] Confirmed GPIO assignments
- [ ] MOS timer dial setting (minutes)
- [ ] Photo / wiring diagram
- [ ] Reservoir + dripper layout
