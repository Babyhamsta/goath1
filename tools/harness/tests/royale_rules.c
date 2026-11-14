/*
ROYALE_RULES.C (test)

The real Royale rules (port/linux/game/royale_rules.c): squads drawn at the
lobby's end, downs, revives, bleed-outs, the gulag, eliminations, placements,
kills and the phases to the results. test_royale_rules.py takes the code
(royale_rules.inc, its header and its body).
*/

#include "harness.h"

#include "royale_rules.inc"

static struct royale_rules_settings settings =
{
	10, /* lobby_ticks */
	100, /* insertion_ticks */
	30, /* bleed_out_ticks */
	5, /* revive_ticks */
	20, /* results_ticks */
	2, /* endgame_squads */
	TRUE, /* skip_insertion */
};

static struct royale_rules rules;
static long revivers[ROYALE_RULES_MAXIMUM_SLOTS];
static byte events[ROYALE_RULES_MAXIMUM_SLOTS];
static int gulag_takes;
static int gulag_calls;

static boolean gulag(long slot, long killer)
{
	(void)slot;
	(void)killer;
	gulag_calls++;
	if (gulag_takes > 0)
	{
		gulag_takes--;
		return TRUE;
	}
	return FALSE;
}

static void tick(void)
{
	royale_rules_tick(&rules, &settings, revivers, events, 1234, gulag);
}

/* a game of count players, past the lobby: in PLAY */
static void start(long count, unsigned long seed)
{
	long slot;

	royale_rules_reset(&rules);
	for (slot = 0; slot < ROYALE_RULES_MAXIMUM_SLOTS; slot++)
		revivers[slot] = NONE;
	gulag_takes = 0;
	gulag_calls = 0;
	for (slot = 0; slot < count; slot++)
		royale_rules_join(&rules, slot);
	while (rules.phase == ROYALE_RULES_PHASE_LOBBY)
		royale_rules_tick(&rules, &settings, revivers, events, seed, gulag);
	tick();
	CHECK(rules.phase == ROYALE_RULES_PHASE_PLAY || rules.phase == ROYALE_RULES_PHASE_ENDGAME,
		"phase %d after the lobby", rules.phase);
}

/* a slot downed by attacker, its unit then killed by the rules' word */
static void bleed_out(long slot)
{
	int guard = 0;

	while (!(events[slot] & ROYALE_RULES_EVENT_BLED_OUT) && guard++ < 1000)
		tick();
	CHECK(events[slot] & ROYALE_RULES_EVENT_BLED_OUT, "slot %ld never bled out", slot);
	royale_rules_died(&rules, &settings, slot, NONE, gulag);
}

static long enemy_of(long slot)
{
	long other;

	for (other = 0; other < ROYALE_RULES_MAXIMUM_SLOTS; other++)
	{
		if (rules.slots[other].state != ROYALE_RULES_NONE && other != slot &&
			rules.slots[other].squad != rules.slots[slot].squad)
		{
			return other;
		}
	}
	return NONE;
}

static void case_squads(void)
{
	long slot;
	int count[ROYALE_RULES_MAXIMUM_SQUADS] = { 0 };
	byte first[6];

	start(6, 77);
	CHECK(rules.squad_count == 3, "%d squads of 6 players", rules.squad_count);
	CHECK(rules.squads_alive == 3, "%d squads alive", rules.squads_alive);
	for (slot = 0; slot < 6; slot++)
	{
		CHECK(rules.slots[slot].squad < 3, "slot %ld squad %d", slot, rules.slots[slot].squad);
		CHECK(rules.slots[slot].state == ROYALE_RULES_ALIVE, "slot %ld state %d", slot, rules.slots[slot].state);
		count[rules.slots[slot].squad]++;
		first[slot] = rules.slots[slot].squad;
	}
	CHECK(count[0] == 2 && count[1] == 2 && count[2] == 2, "squads of %d %d %d", count[0], count[1], count[2]);
	/* (the same seed, the same squads) */
	start(6, 77);
	for (slot = 0; slot < 6; slot++)
		CHECK(rules.slots[slot].squad == first[slot], "slot %ld drew another squad", slot);
}

static void case_revive(void)
{
	long a = 0;
	long mate;
	long enemy;
	int index;

	start(4, 5);
	mate = royale_rules_squadmate(&rules, a);
	enemy = enemy_of(a);
	CHECK(mate != NONE && enemy != NONE, "no squadmate or enemy");
	CHECK(royale_rules_lethal(&rules, &settings, a, enemy), "an ALIVE slot with a squadmate is not downed");
	CHECK(rules.slots[a].state == ROYALE_RULES_DOWNED, "state %d", rules.slots[a].state);
	/* (a downed slot hit again dies) */
	CHECK(!royale_rules_lethal(&rules, &settings, a, enemy), "a downed slot was downed again");
	/* (an enemy's hold revives nothing) */
	revivers[a] = enemy;
	for (index = 0; index < 10; index++)
		tick();
	CHECK(rules.slots[a].state == ROYALE_RULES_DOWNED, "an enemy revived them");
	/* (a revive let go starts over) */
	revivers[a] = mate;
	for (index = 0; index < (int)settings.revive_ticks - 1; index++)
		tick();
	revivers[a] = NONE;
	tick();
	revivers[a] = mate;
	for (index = 0; index < (int)settings.revive_ticks - 1; index++)
		tick();
	CHECK(rules.slots[a].state == ROYALE_RULES_DOWNED, "a revive let go went on");
	tick();
	CHECK(rules.slots[a].state == ROYALE_RULES_ALIVE, "not revived: state %d", rules.slots[a].state);
	CHECK(events[a] & ROYALE_RULES_EVENT_REVIVED, "no revive event");
}

static void case_bleed_out(void)
{
	long a = 0;
	long mate;
	long enemy;

	start(4, 5);
	mate = royale_rules_squadmate(&rules, a);
	enemy = enemy_of(a);
	CHECK(royale_rules_lethal(&rules, &settings, a, enemy), "not downed");
	bleed_out(a);
	CHECK(rules.slots[a].state == ROYALE_RULES_SPECTATING, "state %d with a squadmate in", rules.slots[a].state);
	CHECK(rules.slots[enemy].kills == 1, "the downer has %d kills", rules.slots[enemy].kills);
	/* (the last of a squad is not downed: they die) */
	CHECK(!royale_rules_lethal(&rules, &settings, mate, enemy), "the squad's last was downed");
	royale_rules_died(&rules, &settings, mate, enemy, gulag);
	CHECK(rules.slots[mate].state == ROYALE_RULES_ELIMINATED && rules.slots[a].state == ROYALE_RULES_ELIMINATED,
		"states %d %d with the squad out", rules.slots[a].state, rules.slots[mate].state);
	CHECK(rules.slots[enemy].kills == 2, "the killer has %d kills", rules.slots[enemy].kills);
	CHECK(rules.phase == ROYALE_RULES_PHASE_RESULTS, "phase %d with one squad left", rules.phase);
	CHECK(rules.winner == rules.slots[enemy].squad, "winner %d", rules.winner);
	CHECK(rules.placement[rules.slots[a].squad] == 2, "the squad out placed %d", rules.placement[rules.slots[a].squad]);
	CHECK(rules.placement[rules.winner] == 1, "the winner placed %d", rules.placement[rules.winner]);
}

static void case_gulag(void)
{
	long a = 0;
	long enemy;

	start(4, 9);
	enemy = enemy_of(a);
	gulag_takes = 1;
	CHECK(royale_rules_lethal(&rules, &settings, a, enemy), "not downed");
	bleed_out(a);
	CHECK(rules.slots[a].state == ROYALE_RULES_GULAG, "state %d after the gulag took them", rules.slots[a].state);
	CHECK(gulag_calls == 1, "the gulag was asked %d times", gulag_calls);
	/* (a death in the gulag: asked again, and out) */
	royale_rules_died(&rules, &settings, a, enemy, gulag);
	CHECK(gulag_calls == 2, "the gulag was asked %d times", gulag_calls);
	CHECK(rules.slots[a].state == ROYALE_RULES_SPECTATING, "state %d after losing the gulag", rules.slots[a].state);
	/* (a gulag winner back in) */
	royale_rules_reset(&rules);
	start(4, 9);
	gulag_takes = 1;
	royale_rules_died(&rules, &settings, a, enemy_of(a), gulag);
	royale_rules_set_state(&rules, &settings, a, ROYALE_RULES_ALIVE);
	CHECK(rules.slots[a].state == ROYALE_RULES_ALIVE, "state %d after winning the gulag", rules.slots[a].state);
}

static void case_results(void)
{
	long slot;
	int index;

	start(6, 3);
	/* (one squad out: two left, the endgame) */
	for (slot = 0; slot < 6; slot++)
	{
		if (rules.slots[slot].squad == 0)
			royale_rules_died(&rules, &settings, slot, NONE, gulag);
	}
	CHECK(rules.phase == ROYALE_RULES_PHASE_ENDGAME, "phase %d with two squads left", rules.phase);
	CHECK(rules.placement[0] == 3, "the first out placed %d", rules.placement[0]);
	for (slot = 0; slot < 6; slot++)
	{
		if (rules.slots[slot].squad == 1)
			royale_rules_died(&rules, &settings, slot, NONE, gulag);
	}
	CHECK(rules.phase == ROYALE_RULES_PHASE_RESULTS && rules.winner == 2, "phase %d winner %d", rules.phase,
		rules.winner);
	for (index = 0; index < (int)settings.results_ticks; index++)
		tick();
	CHECK(rules.phase == ROYALE_RULES_PHASE_OVER, "phase %d after the results", rules.phase);
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "squads"))
		case_squads();
	else if (!strcmp(name, "revive"))
		case_revive();
	else if (!strcmp(name, "bleed-out"))
		case_bleed_out();
	else if (!strcmp(name, "gulag"))
		case_gulag();
	else if (!strcmp(name, "results"))
		case_results();
	else
		return 2;
	return 0;
}
