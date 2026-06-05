#pragma once

#include <cstddef>
#include <cstdint>

namespace ui {

/**
 * Round flight-detail view (layout uses config display size; safe on circular panels).
 * Knob/tap on radar opens closest or tapped aircraft; encoder cycles targets on this screen.
 */
void flightDetailDraw();

/** Rebuild list sorted by distance; select closest aircraft. */
void flightDetailSelectClosest();

/** Select aircraft nearest screen tap (radar coordinates). */
void flightDetailSelectAtScreen(int16_t x, int16_t y);

/** Cycle selection (+1 / -1). Returns false if no aircraft. */
bool flightDetailCycle(int delta);

}  // namespace ui
