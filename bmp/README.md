# bmp/

Bitmap art for Ruby's expressions. `RubySpriteRenderer` (`src/ruby/RubySpriteRenderer.cpp`) loads
these **from the SD card**, not from the compiled firmware — copy this whole folder onto the SD
card's root (so the device sees `/bmp/excited.bmp`, etc.) for it to take effect. If a file is
missing, that expression falls back to the procedural silhouette instead of failing.

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
