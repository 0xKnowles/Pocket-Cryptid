# bmp/

Bitmap image assets for Ruby, added here manually (not committed by the firmware build itself).

This directory is not yet wired into the firmware's rendering pipeline — `RubySpriteRenderer`
currently draws Ruby procedurally (no bitmap assets, see `src/ruby/RubySpriteRenderer.h`). Once
image format/size requirements are settled, loading code will be added to draw from files placed
here instead of (or alongside) the procedural renderer.
