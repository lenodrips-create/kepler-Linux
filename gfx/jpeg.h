#pragma once

#include "gfx.h"

/* Decode a baseline JPEG into a new opaque image. */
bool jpeg_decode(const uint8_t *data, size_t len, image_t **out);
