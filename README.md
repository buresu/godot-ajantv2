# godot-ajantv2

Godot 4 GDExtension for AJA NTV2 capture and output.

`AJAOutput` exposes the device, channel, SDI/HDMI destination, video format,
and pixel format as properties. Its output resolution is derived from the
selected AJA video format (`get_width()` / `get_height()`), rather than set as
an independent property.

Output supports 8-bit YCbCr (UYVY), 10-bit YCbCr (v210), and ABGR when the
selected device/channel advertises them. Auto mode prefers YUV8 because its
lower host-DMA bandwidth is more reliable for high-frame-rate output on older
cards.

The output path uses asynchronous GPU readback when the renderer exposes a
compatible RGBA8/BGRA8 texture, retains only the newest rendered frame, and
runs color conversion separately from the high-priority AJA transfer thread.
Two page-aligned DMA buffers and AutoCirculate preroll keep the card clock fed
without building a latency-producing frame queue.
