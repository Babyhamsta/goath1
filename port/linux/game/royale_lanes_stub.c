/*
ROYALE_LANES_STUB.C

Each Royale lane's calls as weak no-ops, so the game links before (and
without) a lane's own royale_<lane>.c: a lane's definitions replace these.
*/

#include "royale.h"

#define ROYALE_WEAK __attribute__((weak))

ROYALE_WEAK void royale_loot_start(struct royale_map_config const *config) { (void)config; }
ROYALE_WEAK void royale_loot_tick(void) {}
ROYALE_WEAK void royale_loot_draw(void) {}

ROYALE_WEAK void royale_beam_start(struct royale_map_config const *config) { (void)config; }
ROYALE_WEAK void royale_beam_tick(void) {}
ROYALE_WEAK void royale_beam_draw(void) {}

ROYALE_WEAK void royale_insertion_start(struct royale_map_config const *config) { (void)config; }
ROYALE_WEAK void royale_insertion_tick(void) {}
ROYALE_WEAK void royale_insertion_draw(void) {}

ROYALE_WEAK void royale_gulag_start(struct royale_map_config const *config) { (void)config; }
ROYALE_WEAK void royale_gulag_tick(void) {}
ROYALE_WEAK void royale_gulag_draw(void) {}
ROYALE_WEAK boolean royale_gulag_eligible(long player_index) { (void)player_index; return FALSE; }
ROYALE_WEAK void royale_gulag_enter(long player_index) { (void)player_index; }

ROYALE_WEAK void royale_map_start(struct royale_map_config const *config) { (void)config; }
ROYALE_WEAK void royale_map_tick(void) {}

ROYALE_WEAK void royale_bots_start(struct royale_map_config const *config) { (void)config; }
ROYALE_WEAK void royale_bots_tick(void) {}

ROYALE_WEAK void royale_scale_start(struct royale_map_config const *config) { (void)config; }
ROYALE_WEAK void royale_scale_tick(void) {}
