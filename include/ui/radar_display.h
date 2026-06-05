#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Incremental sweep animation (~30 fps); does not full-screen blit. */
void radarDisplayRefreshSweep();

/** Blit cached grid + aircraft (after ADSB/range change). */
void radarDisplayBlitStatic();

/** Redraw grid + sweep + aircraft (call ~30 fps while radar is visible). */
void radarDisplayRefreshFrame();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Redraw radar after scale band change. Use after scaleIncrease/Decrease(). */
void radarDisplayRefreshRange();

}  // namespace ui
