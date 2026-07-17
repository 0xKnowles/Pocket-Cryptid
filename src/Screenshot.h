#pragma once

class GfxRenderer;

// Dumps the current framebuffer to a monochrome .bmp on the SD card (see Screenshot.cpp for the
// format details). Returns false if the SD card isn't ready or the write failed; failures are
// logged, not surfaced on-screen — this is meant to be cheap enough to wire to a button press
// without needing its own confirmation UI.
bool saveScreenshot(const GfxRenderer& renderer);
