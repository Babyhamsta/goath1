/* royale_cinema.h: Royale's presentation moments: the island flyover before boarding, the winners' victory orbit and the postgame results screen (port/linux/game/royale_cinema.c)

Core (royale.c) calls these once each; this module owns everything inside
them. Presentation only: read synced Royale state, never change it, so every
screen shows the same thing. */
#ifndef HALO_ROYALE_CINEMA_H
#define HALO_ROYALE_CINEMA_H

#include "cseries.h"

struct royale_map_config;

void royale_cinema_start(struct royale_map_config const *config);
void royale_cinema_reset(void);
void royale_cinema_tick(void);
/* the screen's drawing for this local player (after the world) */
void royale_cinema_draw(short local_player_index);

#endif
