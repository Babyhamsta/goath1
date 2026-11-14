/* royale_loot.h: Royale's loot (port/linux/game/royale_loot.c)

Core calls royale_loot_start, royale_loot_tick and royale_loot_postspawn
(royale.h); royale_loot_item_tier is the bots'. The plan (what lies at each
loot spot, and each supply pod's time, spot and contents) is a pure function
of the match's seed and the map config, so every machine draws the same. */
#ifndef HALO_ROYALE_LOOT_H
#define HALO_ROYALE_LOOT_H

#include "royale.h"

/* the kinds of loot, by rarity */
enum royale_loot_rarity
{
	ROYALE_LOOT_RARITY_COMMON = 0, /* the ground's guns and grenades */
	ROYALE_LOOT_RARITY_RARE, /* the power weapons */
	ROYALE_LOOT_RARITY_POWERUP, /* camouflage, overshield, health */
	NUMBER_OF_ROYALE_LOOT_RARITIES
};

enum royale_loot_kind
{
	ROYALE_LOOT_ASSAULT_RIFLE = 0,
	ROYALE_LOOT_PISTOL,
	ROYALE_LOOT_SHOTGUN,
	ROYALE_LOOT_PLASMA_PISTOL,
	ROYALE_LOOT_PLASMA_RIFLE,
	ROYALE_LOOT_NEEDLER,
	ROYALE_LOOT_FRAG_GRENADES,
	ROYALE_LOOT_PLASMA_GRENADES,
	ROYALE_LOOT_SNIPER_RIFLE,
	ROYALE_LOOT_ROCKET_LAUNCHER,
	ROYALE_LOOT_FUEL_ROD,
	ROYALE_LOOT_CAMOUFLAGE,
	ROYALE_LOOT_OVERSHIELD,
	ROYALE_LOOT_HEALTH_PACK,
	NUMBER_OF_ROYALE_LOOT_KINDS,
	ROYALE_LOOT_KIND_NONE = 0xFF
};

#define ROYALE_LOOT_POD_COUNT 3
#define ROYALE_LOOT_POD_ITEMS 3

struct royale_loot_pod_plan
{
	short spot; /* the config's pod spot */
	short drop_second; /* seconds into PLAY it starts to fall */
	byte kinds[ROYALE_LOOT_POD_ITEMS];
};

struct royale_loot_plan
{
	short spot_count;
	byte spot_kinds[ROYALE_MAXIMUM_LOOT_SPOTS]; /* ROYALE_LOOT_KIND_NONE: empty */
	short pod_count;
	struct royale_loot_pod_plan pods[ROYALE_LOOT_POD_COUNT];
};

/* the plan from the seed, the spots' tiers (enum royale_loot_tier) and the
kinds the map has (a flag per enum royale_loot_kind) */
void royale_loot_plan_make(unsigned long seed, short const *spot_tiers, short spot_count, short pod_spot_count,
	unsigned long available, struct royale_loot_plan *plan);
/* the plan this game drew (royale_loot_start) */
struct royale_loot_plan const *royale_loot_plan_get(void);

/* (core) after a player's unit spawns: the start kit, a pistol low on ammo */
void royale_loot_postspawn(long player_index);

#endif
