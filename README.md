# Modern PT2

A modernized port of the classic **Modern** Pebble watchface, updated for Pebble SDK 4.x and the Pebble Time 2 (Emery, 200×228).

## Credits

**Original watchface:** [Modern by Łukasz Zalewski (Zalew)](https://apps.repebble.com/modern_52bb213af9846878c200015b)
**Original source:** https://github.com/zalew/pebble-watchface-modern *(v2.2 – the basis for this port)*

All credit for the original design, hand geometry, and watchface concept goes to Łukasz Zalewski. This port is a non-commercial project intended to bring the watchface to newer Pebble hardware.

---

## What's New in PT2

| Feature | Original (v2.2) | PT2 |
|---------|----------------|-----|
| SDK | 1.x (PBL_APP_INFO, pbl_main) | 4.x (appinfo.json, main()) |
| Platforms | OG Pebble, Steel | + Time, Time Steel, 2, Round, Time 2 (Emery) |
| Emery scaling | ✗ | ✓ Native 200×228 via `PBL_IF_EMERY_ELSE` |
| Configuration | Compile-time `#define` flags | Runtime via **Clay** settings page |
| Languages | 30 compiled-in via `lang.h` | System locale via `strftime` |
| Weather | ✗ | ✓ Open-Meteo (no API key required) |
| Health data | ✗ | ✓ Heart rate + steps via HealthService |
| Info slots | Date only | 3 configurable slots (left/right/bottom) |
| Hand styles | Solid only | Solid / Outline / Skeleton |
| Battery indicator | ✗ | ✓ Bar under "pebble" logo |
| Background | Bitmap PNG | Programmatic drawing (scales to any screen) |

---

## Configurable Info Slots

Each of the three slots can independently show:

- **None**
- **Weather** — temperature + condition (via [Open-Meteo](https://open-meteo.com/), no API key needed)
- **Date** — weekday + day number in watch system locale
- **Heart Rate** — live BPM (Pebble Health; not available on Aplite)
- **Steps** — daily step count (Pebble Health; not available on Aplite)
- **Battery** — charge percentage

---

## Settings (via Pebble app → watchface settings)

| Setting | Options | Default |
|---------|---------|---------|
| Left slot (9 o'clock) | None / Weather / Date / Heart Rate / Steps / Battery | None |
| Right slot (3 o'clock) | same | Date |
| Bottom slot (6 o'clock) | same | Weather |
| Hand style | Solid / Outline / Skeleton | Solid |
| Show seconds hand | On / Off | Off |
| Hourly vibration | On / Off | Off |
| Light background | On / Off | Off |

---

## Building

Requires the [Rebble SDK](https://rebble.io/) (Pebble SDK 4.x toolchain).

```bash
npm install          # fetches pebble-clay
pebble build         # builds for all platforms
pebble install --emulator emery   # test on Emery emulator
```

---

## License

The DigitalDream Narrow font is © [pizzadude.dk](https://www.pizzadude.dk/) — see `resources/src/fonts/pizzadude.dk License.txt`.
The Orbitron font is © [The League of Moveable Type](https://www.theleagueofmoveabletype.com/) — SIL OFL 1.1.
All other code in this repository is released under the MIT License.
