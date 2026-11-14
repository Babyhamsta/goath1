/* royale_hud.h: Royale's HUD in Halo's own style: squads and players left, the teammate on the tracker and nav marker, the beam warning (port/linux/game/royale_hud.c)

Core (royale.c) calls these once each; this module owns everything inside
them. Presentation only: read synced Royale state, never change it, so every
screen shows the same thing. */
#ifndef HALO_ROYALE_HUD_H
#define HALO_ROYALE_HUD_H

#include "cseries.h"

struct royale_map_config;

void royale_hud_start(struct royale_map_config const *config);
void royale_hud_reset(void);
void royale_hud_tick(void);
/* the screen's drawing for this local player (after the world) */
void royale_hud_draw(short local_player_index);

#endif
