// Colours as the CPU packs them, unpacked alike by every shader that reads one.

// A colour as the CPU packs it (core::UiColours::Of()): linear premultiplied RGBA8, red in the low byte.
float4 UnpackColour(uint colour) {
    return float4(colour & 0xFF, (colour >> 8) & 0xFF, (colour >> 16) & 0xFF, colour >> 24) / 255.0;
}
