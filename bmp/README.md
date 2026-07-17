# bmp/

Source art for Ruby's expressions. These files are baked directly into the firmware image at
build time — see `scripts/generate_embedded_art.py`, which re-encodes each one as an 8bpp
grayscale header under `src/ruby/embeddedArt/` (checked into the repo, regenerate after changing
anything here) — so a freshly flashed device shows this art immediately with **no SD card setup
required**.

Optionally, a device's SD card can still override any of these at `/bmp/<name>.bmp` on the card's
root (`RubySpriteRenderer` checks there first); that's for swapping in custom art without
recompiling, not something you need to do to see the built-in art.

| File | Shown when Ruby is... |
| --- | --- |
| `excited.bmp` | `EXCITED` — a handshake was captured moments ago |
| `curious.bmp` | `CURIOUS` — any new unique device was seen moments ago |
| `content.bmp` | `CONTENT` — steady recent activity |
| `bored.bmp` | `BORED` — quiet for a while |
| `lonely.bmp` | `LONELY` — quiet for a long while |
| `sleep.bmp` | `SLEEPING` — device is on the sleep screen; also used for the boot splash |

Format: standard Windows BMP (`BM` signature), 1/2/4/8/24/32 bpp all work; 32bpp may be either
`BI_RGB` or `BI_BITFIELDS` compression (common from tools that export a `BITMAPV5HEADER` for
alpha-channel art — the alpha channel itself is ignored, only RGB is used). Images are scaled
down (never up) and centered to fit their box, so non-square art is fine — but the scaling is
simple nearest-neighbor, not smoothed, so art pre-sized close to its target box will look
crisper. Box sizes: 170×170 on the dashboard, 120×120 on the boot splash, 96×96 on the sleep
screen.

To replace this art: overwrite the relevant file(s) here, then run
`python3 scripts/generate_embedded_art.py` (needs `Pillow`, see `scripts/requirements.txt`) and
rebuild — the SD-card override path also still works if you'd rather not touch these source files
or rebuild at all.
