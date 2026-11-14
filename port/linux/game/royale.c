/*
ROYALE.C

Royale, GOAT H1's battle royale (royale.h): the rules, phases, squads and
player states, and the lanes' calls. (First cut: Royale is never active yet.)
*/

#include "royale.h"

static struct
{
	boolean active;
	enum royale_phase phase;
	long phase_time;
	long match_time;
	struct royale_map_config config;
} royale;

boolean royale_active(void) { return royale.active; }
enum royale_phase royale_phase(void) { return royale.active ? royale.phase : ROYALE_PHASE_NONE; }
long royale_phase_time(void) { return royale.phase_time; }
long royale_match_time(void) { return royale.match_time; }

short royale_squad_of(long player_index) { (void)player_index; return NONE; }
boolean royale_squad_alive(short squad) { (void)squad; return FALSE; }
long royale_squad_member(short squad, short seat) { (void)squad; (void)seat; return NONE; }
long royale_squadmate_of(long player_index) { (void)player_index; return NONE; }
short royale_squad_count(void) { return 0; }
short royale_squads_alive(void) { return 0; }

enum royale_player_state royale_player_state(long player_index) { (void)player_index; return ROYALE_PLAYER_NONE; }
void royale_set_player_state(long player_index, enum royale_player_state state) { (void)player_index; (void)state; }
long royale_downed_ticks_left(long player_index) { (void)player_index; return 0; }
real royale_revive_progress(long player_index) { (void)player_index; return 0.0f; }
long royale_spectate_target(long player_index) { (void)player_index; return NONE; }

short royale_squad_placement(short squad) { (void)squad; return 0; }
short royale_player_kills(long player_index) { (void)player_index; return 0; }
short royale_winning_squad(void) { return NONE; }

struct royale_map_config const *royale_map_config_get(void) { return royale.active ? &royale.config : NULL; }

boolean royale_map_config_load(char const *map_name, struct royale_map_config *config)
{
	(void)map_name;
	csmemset(config, 0, sizeof(*config));
	return FALSE;
}

boolean royale_map_config_parse(char const *text, struct royale_map_config *config)
{
	(void)text;
	csmemset(config, 0, sizeof(*config));
	return FALSE;
}

long royale_seconds_to_ticks(real seconds) { return (long)(seconds * 30.0f); }
