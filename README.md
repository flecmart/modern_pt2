# Modern PT2

A modernized port of the classic **Modern** Pebble watchface, updated for Pebble SDK 4.x and the Pebble Time 2 (Emery, 200×228).

## Credits

**Original watchface:** [Modern by Łukasz Zalewski (Zalew)](https://apps.repebble.com/modern_52bb213af9846878c200015b)
**Original source:** https://github.com/zalew/pebble-watchface-modern *(v2.2 – the basis for this port)*

All credit for the original design, hand geometry, and watchface concept goes to Łukasz Zalewski. This port is a non-commercial project intended to bring my all time favorite watchface to my new pebble time 2.

---

## What's New in PT2

| Feature | Original (v2.2) | PT2 |
|---------|----------------|-----|
| SDK | 1.x (PBL_APP_INFO, pbl_main) | 4.x (appinfo.json, main()) |
| Platform | OG Pebble, Steel | Pebble Time 2 (Emery) |
| Configuration | Compile-time `#define` flags | Runtime via **Clay** settings page |
| Languages | 30 compiled-in via `lang.h` | System locale via `strftime` |
| Weather | ✗ | ✓ Open-Meteo (no API key required) |
| Health data | ✗ | ✓ Heart rate + steps via HealthService |
| Info slots | Date only | 3 configurable slots (left/right/bottom) |
| Hand styles | Solid only | Solid / Outline / Diamond |
| Accent color | ✗ | ✓ 11 color presets for info & logo |
| Battery indicator | ✗ | ✓ Bar under "pebble" logo |
| Background | Bitmap PNG | Bitmap PNG (native 200×228) |

---

## Configurable Info Slots

Each of the three slots can independently show:

- **None**
- **Weather** — temperature + condition icon (via [Open-Meteo](https://open-meteo.com/), no API key needed)
- **Date** — weekday + day number in watch system locale
- **Heart Rate** — live BPM with heart icon
- **Steps** — daily step count with steps icon
- **Battery** — charge percentage with battery icon

---

## Settings (via Pebble app → watchface settings)

| Setting | Options | Default |
|---------|---------|---------|
| Left slot (9 o'clock) | None / Weather / Date / Heart Rate / Steps / Battery | Weather |
| Right slot (3 o'clock) | same | Heart Rate |
| Bottom slot (6 o'clock) | same | Date |
| Hand style | Solid / Outline / Diamond | Diamond |
| Info & Logo Color | White / Cyan / Electric Blue / Mint Green / Green / Yellow / Chrome Yellow / Orange / Red / Magenta / Vivid Violet | White |
| Use Fahrenheit (°F) | On / Off | Off |
| Show seconds hand | On / Off | Off |
| Hourly vibration | On / Off | Off |

---

## Building

Requires the [Pebble SDK](https://developer.repebble.com/sdk/) (4.x toolchain).

```bash
npm install                       # fetches pebble-clay
pebble build                      # builds for Emery
pebble install --emulator emery   # test on Emery emulator
```

---

## License

The DigitalDream Narrow font is © [pizzadude.dk](https://www.pizzadude.dk/) — see `resources/src/fonts/pizzadude.dk License.txt`.
The Orbitron font is © [The League of Moveable Type](https://www.theleagueofmoveabletype.com/) — SIL OFL 1.1.
All other code in this repository is released under the MIT License.
