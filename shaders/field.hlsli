// The sprites' distance fields (gfx::SpriteArray::field, gfx::DistanceField()), read alike by every shader that samples them.

// A distance field holds signed texels from -FIELD_REACH_TEXELS (inside) to +FIELD_REACH_TEXELS (outside) as 0 to 1, the
// sprite's edge at one half (core::FIELD_REACH_TEXELS; gfx::DistanceField()).
static const float FIELD_REACH_TEXELS = 16.0;

// A sample of a field as texels from the sprite's edge, negative inside.
float FieldTexels(float value) {
    return (value - 0.5) * (2.0 * FIELD_REACH_TEXELS);
}
