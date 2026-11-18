/* The host's datagrams of a sub-step (network_distributed_host_substep_ended,
distributed_forward_flush, the batches): what goes to a machine as a
sub-step ends leaves in one datagram, the forwarded controls with the
sub-step's states whatever the interval since the frame's own forward, and
a tick's last sub-step's with the tick's messages; a datagram is split only
where the next message (or its next entry) does not fit, and the messages
and entries keep their order. A player the client can react to (near its
players or in their sight, distributed_forward_immediate) has its presses
sent in the sub-step's datagram; one it cannot, in the tick's last, unless
it comes where the client can react to it first (distributed_reacts_at). */
#include "harness.h"
typedef unsigned short word;
typedef word message_header;
#define csmemcpy memcpy
#define csmemset memset
#define MIN(a, b) ((a) < (b) ? (a) : (b))
enum { _game_connection_network_server = 2, _game_connection_network_client = 1 };
enum { _error_silent };
#include "config.inc"
enum
{
	HALO_PORT_MAXIMUM_NETWORK_MACHINES = 4,
	MAXIMUM_TRACKED_PLAYERS = 8,
	MAXIMUM_SENDERS = HALO_PORT_MAXIMUM_NETWORK_MACHINES + 1,
	HOST_SENDER = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
	DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE = 40,
	DISTRIBUTED_FORWARDED_INPUT_MAXIMUM_SIZE = 64,
	/* (the states of the sub-steps written, and the tick's message) */
	STATE_SIZE = 18,
	TICK_ENTRY_SIZE = 30,
};
#include "structures.inc"

struct distributed_relayed_action { byte player_index; byte bytes[13]; };
struct distributed_unit_state { byte player_index; };
struct player_datum { long unused; };

static struct
{
	boolean pending;
	boolean valid;
	long tick;
	struct distributed_relayed_action action;
	boolean changed;
} distributed_forwards[MAXIMUM_TRACKED_PLAYERS];
static byte distributed_forward_periods[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_TRACKED_PLAYERS];
static long distributed_forwarded_ticks[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_TRACKED_PLAYERS];
static long distributed_forwarded_times[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_TRACKED_PLAYERS];
static struct live_control_far distributed_far_live[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_TRACKED_PLAYERS];
static unsigned long distributed_checkpoint_epoch;
static boolean distributed_checkpoint_synchronized = TRUE;
static long distributed_batch_bytes[MAXIMUM_SENDERS];
static long distributed_batch_datagrams[MAXIMUM_SENDERS];
static struct { long sent; } distributed_statistics;
static struct distributed_batch distributed_batches[MAXIMUM_SENDERS];
static struct distributed_packer distributed_packer;
static boolean distributed_forward_held;
static boolean distributed_forward_immediate[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_TRACKED_PLAYERS];
static boolean distributed_far_owed[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_TRACKED_PLAYERS];

/* the world: machines 1 and 2, OpenH1 clients; players near both (period 1)
of states at the sub-steps, and players whose live controls are pending */
static long now_time = 100;
static unsigned long long now_microseconds = 1000000;
static boolean near[MAXIMUM_TRACKED_PLAYERS];
static boolean live_pending[MAXIMUM_TRACKED_PLAYERS];
static struct player_datum players[MAXIMUM_TRACKED_PLAYERS];
/* players further (alive, their states not sent at the sub-steps), their
presses as the host's sub-steps ran them (pending: made this sub-step), and
where each client can react to them now (the plan's tests, as they move) */
static boolean far[MAXIMUM_TRACKED_PLAYERS];
static boolean far_pressed[MAXIMUM_TRACKED_PLAYERS];
static boolean reacts_now[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_TRACKED_PLAYERS];

static long game_time_get(void) { return now_time; }
static short game_connection(void) { return _game_connection_network_server; }
static boolean game_in_progress(void) { return TRUE; }
static boolean network_standard_hosting(void) { return TRUE; }
static boolean network_standard_machine_is_openh1(long machine_index) { (void)machine_index; return TRUE; }
static boolean network_standard_message_goes_to(long machine_index, byte type)
{
	(void)machine_index; (void)type; return TRUE;
}
static boolean halo_server_mode(void) { return FALSE; }
static unsigned long long platform_shared_clock_microseconds(void) { return now_microseconds; }
static void network_clock_frame_read(void) {}
static boolean network_substeps_active(void) { return TRUE; }
static void error(int kind, char const *format, ...) { (void)kind; (void)format; }
static void build_message_header(message_header *header, word size, int a, int b) { (void)a; (void)b; *header = size; }
static short distributed_client_machines(long *indices, short maximum)
{
	(void)maximum;
	indices[0] = 1;
	indices[1] = 2;
	return 2;
}
static boolean distributed_machine_has_player(long machine_index, short player_index)
{
	(void)machine_index; (void)player_index; return FALSE;
}
static word distributed_relayed_action_write(struct distributed_relayed_action const *action, byte *buffer)
{
	memcpy(buffer, action, sizeof(*action));
	return sizeof(*action);
}
static word network_substeps_forward_write(short player_index, long key, byte *buffer)
{
	(void)player_index; (void)key; buffer[0] = 0; return 1;
}
static boolean network_substeps_live_own(short player_index) { (void)player_index; return TRUE; }
static word network_substeps_live_forward_write(short player_index, byte *buffer)
{
	if (!live_pending[player_index])
		return 0;
	memset(buffer, 0, LIVE_CONTROL_FORWARD_MAXIMUM_SIZE);
	buffer[0] = (byte)player_index;
	return LIVE_CONTROL_FORWARD_MAXIMUM_SIZE;
}
static boolean network_substeps_live_far_due(short player_index, struct live_control_far *last)
{
	(void)last; return far_pressed[player_index];
}
/* (the latest press, 0x40 + its player first) */
static word network_substeps_live_far_write(short player_index, byte *buffer)
{
	if (!far[player_index])
		return 0;
	memset(buffer, 0, LIVE_CONTROL_FORWARD_MAXIMUM_SIZE);
	buffer[0] = (byte)(0x40 + player_index);
	return LIVE_CONTROL_FORWARD_MAXIMUM_SIZE;
}
static void network_substeps_live_forwarded(void)
{
	memset(live_pending, 0, sizeof(live_pending));
	memset(far_pressed, 0, sizeof(far_pressed));
}
static boolean network_substeps_live_pending(void)
{
	short index;

	for (index = 0; index < MAXIMUM_TRACKED_PLAYERS; index++)
		if (live_pending[index] || far_pressed[index])
			return TRUE;
	return FALSE;
}
static struct player_datum *distributed_player(short player_index)
{
	return near[player_index] || far[player_index] ? &players[player_index] : NULL;
}
static long distributed_living_unit(struct player_datum const *player) { return (long)(player - players); }
static void object_get_origin(long unit_index, real_point3d *origin)
{
	origin->x = (real)unit_index; origin->y = 0.0f; origin->z = 0.0f;
}
static short distributed_object_cluster(long unit_index) { return (short)unit_index; }
static boolean distributed_reacts_at(long machine_index, real_point3d const *origin, short cluster_index)
{
	CHECK((short)origin->x == cluster_index, "the player's own place");
	return reacts_now[machine_index][cluster_index];
}
static void distributed_state_from_player(short player_index, long time, struct distributed_unit_state *state)
{
	(void)time; state->player_index = (byte)player_index;
}
static boolean distributed_substep_state_due(long machine_index, short player_index,
	struct distributed_unit_state const *state)
{
	(void)machine_index; (void)player_index; (void)state; return TRUE;
}
/* (every player asked for: test_substep_requests.py has the test) */
static int config_boolean(char const *name) { (void)name; return 0; }
static boolean distributed_substep_states_wanted(boolean always, long machine_index, short player_index)
{
	(void)always; (void)machine_index; (void)player_index; return TRUE;
}
/* (every state a change: test_substep_state_changes.py has the test) */
static int distributed_substep_sents[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_TRACKED_PLAYERS];
static boolean distributed_substep_state_unchanged(int *sent, struct distributed_unit_state const *state, long now)
{
	(void)sent; (void)state; (void)now; return FALSE;
}
static word distributed_substep_state_write(long machine_index, struct distributed_unit_state const *state,
	byte *buffer)
{
	(void)machine_index;
	memset(buffer, 0xAB, STATE_SIZE);
	buffer[1] = state->player_index;
	return STATE_SIZE;
}

/* the datagrams sent, to each machine */
enum { MAXIMUM_DATAGRAMS = 32 };
static struct { word size; byte data[DATAGRAM_MAXIMUM_SIZE]; } datagrams[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_DATAGRAMS];
static short datagram_count[HALO_PORT_MAXIMUM_NETWORK_MACHINES];

static boolean network_distributed_client_send(void *message, word size) { (void)message; (void)size; return TRUE; }
static boolean network_distributed_server_send_to_machine(long machine_index, void *message, word size)
{
	short index = datagram_count[machine_index]++;

	CHECK(index < MAXIMUM_DATAGRAMS, "too many datagrams");
	CHECK(size <= DATAGRAM_MAXIMUM_SIZE, "a datagram of %d bytes, past the most a datagram has", size);
	datagrams[machine_index][index].size = size;
	memcpy(datagrams[machine_index][index].data, message, size);
	return TRUE;
}

#include "under_test.inc"

/* a datagram's messages: each one's type, count and entries */
struct parsed { byte type; byte count; word size; byte const *entries; };

static short parse(long machine_index, short datagram_index, struct parsed *messages, short maximum)
{
	byte const *data = datagrams[machine_index][datagram_index].data;
	word size = datagrams[machine_index][datagram_index].size;
	word offset = sizeof(struct distributed_message_header);
	short count = 0;

	CHECK(((struct distributed_message_header const *)data)->type == _distributed_message_batch, "a batch");
	while (offset + sizeof(word) <= size)
	{
		word length;
		struct distributed_message_header header;

		memcpy(&length, data + offset, sizeof(length));
		offset += sizeof(word);
		CHECK(offset + length <= size, "a message within its datagram");
		memcpy((byte *)&header + sizeof(message_header), data + offset,
			sizeof(header) - sizeof(message_header));
		CHECK(count < maximum, "too many messages");
		messages[count].type = header.type;
		messages[count].count = header.count;
		messages[count].size = (word)(length - (sizeof(header) - sizeof(message_header)));
		messages[count].entries = data + offset + sizeof(header) - sizeof(message_header);
		count++;
		offset += length;
	}
	CHECK(offset == size, "the datagram's messages fill it");
	return count;
}

static void reset(void)
{
	memset(datagram_count, 0, sizeof(datagram_count));
	memset(near, 0, sizeof(near));
	memset(live_pending, 0, sizeof(live_pending));
	memset(distributed_forward_periods, 0, sizeof(distributed_forward_periods));
	memset(far, 0, sizeof(far));
	memset(far_pressed, 0, sizeof(far_pressed));
	memset(reacts_now, 0, sizeof(reacts_now));
	memset(distributed_forward_immediate, 0, sizeof(distributed_forward_immediate));
	memset(distributed_far_owed, 0, sizeof(distributed_far_owed));
}

/* players near machines 1 and 2: their states at the sub-steps */
static void set_near(short player_index)
{
	near[player_index] = TRUE;
	distributed_forward_periods[1][player_index] = 1;
	distributed_forward_periods[2][player_index] = 1;
	distributed_forward_immediate[1][player_index] = TRUE;
	distributed_forward_immediate[2][player_index] = TRUE;
}

/* a player further from machines 1 and 2, of the plan's period, whom they
can react to (in sight, or near: the plan's word) or not */
static void set_far(short player_index, byte period, boolean reacts)
{
	far[player_index] = TRUE;
	distributed_forward_periods[1][player_index] = period;
	distributed_forward_periods[2][player_index] = period;
	distributed_forward_immediate[1][player_index] = reacts;
	distributed_forward_immediate[2][player_index] = reacts;
}

/* the tick's own message to a machine: entries numbered from first */
static void tick_message(long machine_index, short entries, short first)
{
	static struct { struct distributed_message_header header; byte data[4096]; } message;
	short index;

	for (index = 0; index < entries; index++)
	{
		memset(message.data + index * TICK_ENTRY_SIZE, 0, TICK_ENTRY_SIZE);
		message.data[index * TICK_ENTRY_SIZE] = (byte)(first + index);
	}
	distributed_send_to_machine(machine_index, &message, _distributed_message_unit_state_differences, entries,
		(word)(sizeof(message.header) + entries * TICK_ENTRY_SIZE));
}

/* a sub-step but the last: its states and its live controls, one datagram */
static void substep_case(void)
{
	struct parsed messages[8];
	long machine_index;

	reset();
	set_near(3);
	set_near(4);
	set_near(5);
	live_pending[5] = TRUE;
	network_distributed_host_substep_ended(0, 4);
	for (machine_index = 1; machine_index <= 2; machine_index++)
	{
		short count;

		CHECK(datagram_count[machine_index] == 1, "one datagram to machine %ld, not %d", machine_index,
			datagram_count[machine_index]);
		count = parse(machine_index, 0, messages, 8);
		CHECK(count == 2, "the states and the controls, %d messages", count);
		CHECK(messages[0].type == _distributed_message_substep_states && messages[0].count == 3, "the states first");
		CHECK(messages[0].entries[2 + 1] == 3 && messages[0].entries[2 + STATE_SIZE + 1] == 4 &&
			messages[0].entries[2 + 2 * STATE_SIZE + 1] == 5, "the states in order");
		CHECK(messages[1].type == _distributed_message_forwarded_live_controls && messages[1].count == 1,
			"the controls with them");
	}
}

/* a sub-step that ends just after the frame's own forward (within its
interval): its controls ride with its states, not held for later */
static void substep_after_read_case(void)
{
	struct parsed messages[8];
	long machine_index;

	reset();
	set_near(3);
	live_pending[6] = TRUE;
	distributed_forward_periods[1][6] = 1;
	distributed_forward_periods[2][6] = 1;
	now_microseconds += 1000000;
	network_distributed_frame_received();
	CHECK(datagram_count[1] == 1 && datagram_count[2] == 1, "the frame's forward goes at once");
	CHECK(!live_pending[6], "and is sent");
	live_pending[7] = TRUE;
	distributed_forward_periods[1][7] = 1;
	distributed_forward_periods[2][7] = 1;
	now_microseconds += 1000;
	network_distributed_host_substep_ended(1, 4);
	for (machine_index = 1; machine_index <= 2; machine_index++)
	{
		short count;

		CHECK(datagram_count[machine_index] == 2, "one datagram of the sub-step, not %d",
			datagram_count[machine_index] - 1);
		count = parse(machine_index, 1, messages, 8);
		CHECK(count == 2 && messages[1].type == _distributed_message_forwarded_live_controls &&
			messages[1].entries[0] == 7, "the sub-step's controls ride with its states (%d messages)", count);
	}
	CHECK(!live_pending[7], "none left for later");
}

/* a tick's last sub-step: its controls go with the tick's messages, in one
datagram, first */
static void tick_end_case(void)
{
	struct parsed messages[8];
	long machine_index;

	reset();
	set_near(3);
	live_pending[3] = TRUE;
	now_microseconds += 1000000;
	network_distributed_host_substep_ended(3, 4);
	/* (the tick: network_distributed_tick) */
	now_time++;
	tick_message(1, 4, 0);
	tick_message(2, 4, 0);
	distributed_batches_flush();
	for (machine_index = 1; machine_index <= 2; machine_index++)
	{
		short count;

		CHECK(datagram_count[machine_index] == 1, "one datagram of the tick's end to machine %ld, not %d",
			machine_index, datagram_count[machine_index]);
		count = parse(machine_index, 0, messages, 8);
		CHECK(count == 2, "the controls and the tick's message, %d", count);
		CHECK(messages[0].type == _distributed_message_forwarded_live_controls, "the controls first");
		CHECK(messages[1].type == _distributed_message_unit_state_differences && messages[1].count == 4,
			"then the tick's");
	}
}

/* past a datagram: split where the next entry does not fit, and only there;
the entries in order */
static void split_case(void)
{
	struct parsed messages[8];
	short entries = 100;
	short next = 0;
	short datagram_index;
	boolean controls = FALSE;

	reset();
	set_near(3);
	live_pending[3] = TRUE;
	now_microseconds += 1000000;
	network_distributed_host_substep_ended(3, 4);
	now_time++;
	tick_message(1, entries, 0);
	distributed_batches_flush();
	CHECK(datagram_count[1] >= 3, "%d entries of %d bytes in %d datagrams", entries, TICK_ENTRY_SIZE,
		datagram_count[1]);
	for (datagram_index = 0; datagram_index < datagram_count[1]; datagram_index++)
	{
		short count = parse(1, datagram_index, messages, 8);
		short index;

		for (index = 0; index < count; index++)
		{
			short entry;

			if (messages[index].type == _distributed_message_forwarded_live_controls)
			{
				CHECK(datagram_index == 0 && index == 0, "the controls first");
				controls = TRUE;
				continue;
			}
			CHECK(messages[index].type == _distributed_message_unit_state_differences, "the tick's");
			CHECK(messages[index].size == messages[index].count * TICK_ENTRY_SIZE, "whole entries");
			for (entry = 0; entry < messages[index].count; entry++)
			{
				CHECK(messages[index].entries[entry * TICK_ENTRY_SIZE] == next, "entry %d in order, not %d", next,
					messages[index].entries[entry * TICK_ENTRY_SIZE]);
				next++;
			}
		}
		/* (split only where the next entry would not fit) */
		if (datagram_index < datagram_count[1] - 1)
		{
			CHECK(datagrams[1][datagram_index].size + BATCH_MESSAGE_OVERHEAD + TICK_ENTRY_SIZE > DATAGRAM_MAXIMUM_SIZE,
				"datagram %d split with room for another entry (%d bytes)", datagram_index,
				datagrams[1][datagram_index].size);
		}
	}
	CHECK(controls, "the controls sent");
	CHECK(next == entries, "every entry sent, %d of %d", next, entries);
}

/* a tick's last sub-step whose tick sent nothing after it: the next frame
sends what it held */
static void held_case(void)
{
	reset();
	set_near(3);
	live_pending[3] = TRUE;
	now_microseconds += 1000000;
	network_distributed_host_substep_ended(3, 4);
	CHECK(datagram_count[1] == 0, "held for the tick");
	now_microseconds += 1000;
	network_distributed_frame_received();
	CHECK(datagram_count[1] == 1 && datagram_count[2] == 1, "sent by the next frame");
}

/* whether a datagram has a player's press (forwarded live controls) */
static boolean has_press(long machine_index, short datagram_index, short player_index)
{
	struct parsed messages[8];
	short count = parse(machine_index, datagram_index, messages, 8);
	short index;

	for (index = 0; index < count; index++)
	{
		short entry;

		if (messages[index].type != _distributed_message_forwarded_live_controls)
			continue;
		for (entry = 0; entry < messages[index].count; entry++)
		{
			if (messages[index].entries[entry * LIVE_CONTROL_FORWARD_MAXIMUM_SIZE] == 0x40 + player_index)
				return TRUE;
		}
	}
	return FALSE;
}

/* players the clients can react to: a near one's states and controls, a
far one's press in sight and an unseen but near one's, all in the
sub-step's own datagram, as they went before */
static void react_case(void)
{
	long machine_index;

	reset();
	set_near(3);
	live_pending[3] = TRUE;
	set_far(4, 4, TRUE);
	far_pressed[4] = TRUE;
	set_far(5, 6, TRUE);
	far_pressed[5] = TRUE;
	now_microseconds += 1000000;
	network_distributed_host_substep_ended(0, 4);
	for (machine_index = 1; machine_index <= 2; machine_index++)
	{
		struct parsed messages[8];
		short count;

		CHECK(datagram_count[machine_index] == 1, "one datagram of the sub-step to machine %ld, not %d",
			machine_index, datagram_count[machine_index]);
		count = parse(machine_index, 0, messages, 8);
		CHECK(count == 2 && messages[0].type == _distributed_message_substep_states,
			"the near player's state in it (%d messages)", count);
		CHECK(messages[1].type == _distributed_message_forwarded_live_controls && messages[1].count == 3,
			"the near player's controls and both presses in it");
		CHECK(has_press(machine_index, 0, 4), "the far player in sight's press at once");
		CHECK(has_press(machine_index, 0, 5), "the player unseen but near's press at once");
	}
}

/* a player the clients cannot react to (far and unseen): its press made no
datagram of a sub-step, and went in the tick's last, once */
static void defer_case(void)
{
	long machine_index;

	reset();
	set_far(5, 6, FALSE);
	far_pressed[5] = TRUE;
	now_microseconds += 1000000;
	network_distributed_host_substep_ended(0, 4);
	CHECK(datagram_count[1] == 0 && datagram_count[2] == 0, "no datagram of a sub-step for it");
	now_microseconds += 8000;
	network_distributed_frame_received();
	network_distributed_host_substep_ended(1, 4);
	network_distributed_host_substep_ended(2, 4);
	CHECK(datagram_count[1] == 0 && datagram_count[2] == 0, "none later in the tick either");
	network_distributed_host_substep_ended(3, 4);
	now_time++;
	tick_message(1, 4, 0);
	tick_message(2, 4, 0);
	distributed_batches_flush();
	for (machine_index = 1; machine_index <= 2; machine_index++)
	{
		CHECK(datagram_count[machine_index] == 1, "the tick's datagram alone, not %d", datagram_count[machine_index]);
		CHECK(has_press(machine_index, 0, 5), "the press in the tick's datagram");
	}
	/* (owed no more) */
	network_distributed_host_substep_ended(0, 4);
	CHECK(datagram_count[1] == 1, "sent once");
}

/* a far, unseen player's press owed, then the player comes into sight (or
near) at a later sub-step: it goes in that sub-step's datagram */
static void into_view_case(void)
{
	reset();
	set_far(5, 6, FALSE);
	far_pressed[5] = TRUE;
	now_microseconds += 1000000;
	network_distributed_host_substep_ended(0, 4);
	CHECK(datagram_count[1] == 0 && datagram_count[2] == 0, "owed");
	reacts_now[1][5] = TRUE;
	network_distributed_host_substep_ended(1, 4);
	CHECK(datagram_count[1] == 1 && has_press(1, 0, 5), "into view: the press at once, in sub-step 1's datagram");
	CHECK(datagram_count[2] == 0, "the other client, which cannot react to it, still owed");
	CHECK(distributed_forward_immediate[1][5], "and at once from then on");
	far_pressed[5] = TRUE;
	network_distributed_host_substep_ended(2, 4);
	CHECK(datagram_count[1] == 2 && has_press(1, 1, 5), "its next press at once too");
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "substep"))
		substep_case();
	else if (!strcmp(name, "substep-after-read"))
		substep_after_read_case();
	else if (!strcmp(name, "tick-end"))
		tick_end_case();
	else if (!strcmp(name, "split"))
		split_case();
	else if (!strcmp(name, "held"))
		held_case();
	else if (!strcmp(name, "react"))
		react_case();
	else if (!strcmp(name, "defer"))
		defer_case();
	else if (!strcmp(name, "into-view"))
		into_view_case();
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
