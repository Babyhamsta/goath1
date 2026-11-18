/* The host's datagrams of a sub-step (network_distributed_host_substep_ended,
distributed_forward_flush, the batches): what goes to a machine as a
sub-step ends leaves in one datagram, the forwarded controls with the
sub-step's states whatever the interval since the frame's own forward, and
a tick's last sub-step's with the tick's messages; a datagram is split only
where the next message (or its next entry) does not fit, and the messages
and entries keep their order. */
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

/* the world: machines 1 and 2, OpenH1 clients; players near both (period 1)
of states at the sub-steps, and players whose live controls are pending */
static long now_time = 100;
static unsigned long long now_microseconds = 1000000;
static boolean near[MAXIMUM_TRACKED_PLAYERS];
static boolean live_pending[MAXIMUM_TRACKED_PLAYERS];
static struct player_datum players[MAXIMUM_TRACKED_PLAYERS];

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
	memset(buffer, 0, LIVE_CONTROL_FORWARD_SIZE);
	buffer[0] = (byte)player_index;
	return LIVE_CONTROL_FORWARD_SIZE;
}
static boolean network_substeps_live_far_due(short player_index, struct live_control_far *last)
{
	(void)player_index; (void)last; return FALSE;
}
static word network_substeps_live_far_write(short player_index, byte *buffer)
{
	(void)player_index; (void)buffer; return 0;
}
static void network_substeps_live_forwarded(void) { memset(live_pending, 0, sizeof(live_pending)); }
static boolean network_substeps_live_pending(void)
{
	short index;

	for (index = 0; index < MAXIMUM_TRACKED_PLAYERS; index++)
		if (live_pending[index])
			return TRUE;
	return FALSE;
}
static struct player_datum *distributed_player(short player_index)
{
	return near[player_index] ? &players[player_index] : NULL;
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
}

/* players near machines 1 and 2: their states at the sub-steps */
static void set_near(short player_index)
{
	near[player_index] = TRUE;
	distributed_forward_periods[1][player_index] = 1;
	distributed_forward_periods[2][player_index] = 1;
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
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
