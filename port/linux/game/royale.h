/* royale.h: Royale, GOAT H1's battle royale (port/linux/game/royale.c)

Squads of two drop onto a map from Pelicans, loot, and fight while a
glassing beam sweeps the map; no one respawns. A player killed is downed
first (a squadmate can revive them), then goes to the gulag once, else is
eliminated and spectates a squadmate. The last squad alive wins.

Core (royale.c) owns the rules, the phases, the squads, the player states,
the game type, the map config and the hooks; each lane is a module of its own
(royale_<lane>.c) that core starts, ticks and draws:

	royale_<lane>_start(config)  once, when a Royale game's map is loaded
	royale_<lane>_tick()         once per game tick, in the simulation (the
	                             same on host and clients, run to run)
	royale_<lane>_draw()         each rendered frame, after the HUD (drawing
	                             lanes only)

Every rule is simulation: random choices use the game's seeded random, never
a clock. With Royale off nothing here does anything. */
#ifndef HALO_ROYALE_H
#define HALO_ROYALE_H

#include "cseries.h"
#include "math/real_math.h"

/* ---- limits */

#define ROYALE_MAXIMUM_PLAYERS 64
#define ROYALE_SQUAD_SIZE 2
#define ROYALE_MAXIMUM_SQUADS (ROYALE_MAXIMUM_PLAYERS / ROYALE_SQUAD_SIZE)

/* ---- the game */

/* the current game is a Royale game */
boolean royale_active(void);

/* ---- phases */

enum royale_phase
{
	ROYALE_PHASE_NONE = -1, /* not a Royale game */
	ROYALE_PHASE_LOBBY = 0, /* players gather, squads assigned */
	ROYALE_PHASE_INSERTION, /* Pelicans fly, squads jump */
	ROYALE_PHASE_PLAY, /* the beam sweeps */
	ROYALE_PHASE_ENDGAME, /* the last stage: few squads left, the beam's last sweep */
	ROYALE_PHASE_RESULTS, /* the last squad alive has won: the results screen */
	NUMBER_OF_ROYALE_PHASES
};

enum royale_phase royale_phase(void);
/* the game ticks spent in the current phase */
long royale_phase_time(void);
/* the game ticks since the Royale game began (LOBBY's first tick) */
long royale_match_time(void);

/* ---- squads

A player is the player's datum index (players.h), as everywhere in the
game; a squad is 0 .. ROYALE_MAXIMUM_SQUADS - 1, NONE for none. */

short royale_squad_of(long player_index);
/* the squad has a player not eliminated (alive, downed, falling, in the
gulag ...) */
boolean royale_squad_alive(short squad);
/* the squad's players (NONE where a seat is empty) */
long royale_squad_member(short squad, short seat);
/* the other player of the player's squad, NONE if none */
long royale_squadmate_of(long player_index);
/* squads with a player in the game, and those still alive */
short royale_squad_count(void);
short royale_squads_alive(void);

/* ---- player states */

enum royale_player_state
{
	ROYALE_PLAYER_NONE = -1, /* not in the Royale game */
	ROYALE_PLAYER_IN_PELICAN = 0,
	ROYALE_PLAYER_FALLING,
	ROYALE_PLAYER_ALIVE,
	ROYALE_PLAYER_DOWNED, /* crawling; a squadmate can revive them; bleeds out */
	ROYALE_PLAYER_GULAG,
	ROYALE_PLAYER_SPECTATING, /* eliminated, watching a living squadmate */
	ROYALE_PLAYER_ELIMINATED, /* eliminated, no squadmate left to watch */
	NUMBER_OF_ROYALE_PLAYER_STATES
};

enum royale_player_state royale_player_state(long player_index);
/* lanes move players between their own states: insertion (IN_PELICAN ->
FALLING -> ALIVE), gulag (GULAG -> ALIVE on a win, ELIMINATED on a loss).
Core moves ALIVE -> DOWNED -> ALIVE / GULAG / ELIMINATED. */
void royale_set_player_state(long player_index, enum royale_player_state state);
/* a downed player's bleed-out left, in ticks (0 if not downed), and a
revive's progress 0..1 */
long royale_downed_ticks_left(long player_index);
real royale_revive_progress(long player_index);
/* the player watched by a spectating one, NONE if none */
long royale_spectate_target(long player_index);

/* ---- placement and kills (the results screen) */

/* a squad's place: 1 for the winner, 0 while still alive */
short royale_squad_placement(short squad);
short royale_player_kills(long player_index);
/* the winning squad, NONE until RESULTS */
short royale_winning_squad(void);

/* ---- the map config (port/assets/royale/<map>.toml) */

#define ROYALE_MAXIMUM_FLIGHT_PATHS 16
#define ROYALE_MAXIMUM_BEAM_STAGES 8
#define ROYALE_MAXIMUM_LOOT_SPOTS 512
#define ROYALE_MAXIMUM_POD_SPOTS 32
#define ROYALE_MAXIMUM_BLOCKED_ENTRANCES 64

enum royale_side
{
	ROYALE_SIDE_NORTH = 0, /* +y */
	ROYALE_SIDE_EAST, /* +x */
	ROYALE_SIDE_SOUTH, /* -y */
	ROYALE_SIDE_WEST, /* -x */
	NUMBER_OF_ROYALE_SIDES
};

struct royale_bounds
{
	real_point3d minimum;
	real_point3d maximum;
};

/* a Pelican's flight: in at start, out at end, at this height */
struct royale_flight_path
{
	real_point3d start;
	real_point3d end;
	real speed; /* world units per second */
};

/* one stage of the beam's sweep, as fractions of the map's length from the
side it starts on: from start to end over duration seconds (after a wait of
delay seconds) */
struct royale_beam_stage
{
	real delay;
	real start;
	real end;
	real duration;
	real damage_per_second;
};

enum royale_loot_tier
{
	ROYALE_LOOT_COMMON = 0,
	ROYALE_LOOT_RARE,
	ROYALE_LOOT_EPIC,
	ROYALE_LOOT_LEGENDARY,
	NUMBER_OF_ROYALE_LOOT_TIERS
};

struct royale_loot_spot
{
	real_point3d position;
	short tier; /* enum royale_loot_tier */
};

/* an interior entrance closed off: an axis-aligned box no one passes */
struct royale_blocked_entrance
{
	struct royale_bounds bounds;
};

struct royale_gulag_arena
{
	struct royale_bounds bounds;
	real_point3d spawn[2];
	real spawn_facing[2]; /* radians */
};

struct royale_map_config
{
	char map_name[64]; /* the scenario's name, e.g. "b30" */
	short structure_bsp; /* the BSP played in (b30's exterior) */
	struct royale_bounds bounds; /* the play area: out of it is the beam's */
	real drop_height; /* the Pelicans' height (world z) */

	short flight_path_count;
	struct royale_flight_path flight_paths[ROYALE_MAXIMUM_FLIGHT_PATHS];

	/* the beam starts from a side drawn among these (flag per enum royale_side;
	all if 0) */
	long beam_sides;
	short beam_stage_count;
	struct royale_beam_stage beam_stages[ROYALE_MAXIMUM_BEAM_STAGES];

	short loot_spot_count;
	struct royale_loot_spot loot_spots[ROYALE_MAXIMUM_LOOT_SPOTS];

	short pod_spot_count;
	real_point3d pod_spots[ROYALE_MAXIMUM_POD_SPOTS];

	struct royale_gulag_arena gulag;

	short blocked_entrance_count;
	struct royale_blocked_entrance blocked_entrances[ROYALE_MAXIMUM_BLOCKED_ENTRANCES];
};

/* the config of the map being played (NULL when not a Royale game) */
struct royale_map_config const *royale_map_config_get(void);
/* reads port/assets/royale/<map_name>.toml into config: FALSE (config at its
defaults) if there is none or it does not parse */
boolean royale_map_config_load(char const *map_name, struct royale_map_config *config);
/* the same from text (tests) */
boolean royale_map_config_parse(char const *text, struct royale_map_config *config);

/* ---- rules' timing (game ticks are at game.sim_rate: use these) */

/* seconds in game ticks at the current tick rate */
long royale_seconds_to_ticks(real seconds);

/* ---- the lanes (each in royale_<lane>.c; core calls them) */

void royale_loot_start(struct royale_map_config const *config);
void royale_loot_tick(void);
void royale_loot_draw(void);

void royale_beam_start(struct royale_map_config const *config);
void royale_beam_tick(void);
void royale_beam_draw(void);

void royale_insertion_start(struct royale_map_config const *config);
void royale_insertion_tick(void);
void royale_insertion_draw(void);

void royale_gulag_start(struct royale_map_config const *config);
void royale_gulag_tick(void);
void royale_gulag_draw(void);
/* a downed player bled out or was finished: may they go to the gulag
(core asks; then calls royale_gulag_enter) */
boolean royale_gulag_eligible(long player_index);
void royale_gulag_enter(long player_index);

void royale_map_start(struct royale_map_config const *config);
void royale_map_tick(void);

void royale_bots_start(struct royale_map_config const *config);
void royale_bots_tick(void);

void royale_scale_start(struct royale_map_config const *config);
void royale_scale_tick(void);

/* ---- hooks other lanes ask core for are added below, each with the lane
that asked */

#endif
