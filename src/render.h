#ifndef RUNFORLIFE64_RENDER_H
#define RUNFORLIFE64_RENDER_H

#include <surface.h>

#include "game.h"

void rfl_render_init(void);
void rfl_render(surface_t *surface, surface_t *zbuffer, const RflGame *game);

#endif
