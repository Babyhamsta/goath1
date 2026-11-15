/* royale_audio.c: Royale's sound: the Pelican pilot's radio, campaign music at the big moments and the announcer

Everything here is the map's own sounds (b30's, Silent Cartographer's):

- the drop: the level's own insertion radio, the Pelican pilot's
  (sound\dialog\b30\b30_030_pilot, then b30_032_pilot), over the level's
  opening music (levels\b30\music\b30_01: the Halo theme), which plays out
  as the first squads land;
- the final few squads (ENDGAME): the level's driving music
  (levels\b30\music\b30_05);
- the win (RESULTS): the level's extraction music
  (sound\sinomatixx_music\b30_extraction_music);
- the announcer (sound\dialog\multiplayer1\...): a squad wiped, the final
  two squads and "Game over", where the map has its lines (the stock
  multiplayer maps do; the campaign's do not, and then it says nothing).

Every cue is timed off the synced Royale state (the phase, its clock, the
squads alive), each machine playing its own at the same tick, so every
player hears the same thing. Nothing here changes a rule. */

#include "cseries.h"
#include "sound/game_sound.h"
#include "sound/sound_definitions.h"
#include "tag_files/tag_groups.h"
#include "royale_audio.h"
#include "royale.h"

void platform_log(char const *format, ...);

/* ---------- the sounds */

enum
{
	_audio_radio_pilot_1 = 0,
	_audio_radio_pilot_2,
	_audio_announcer_wipe,
	_audio_announcer_final_two,
	_audio_announcer_game_over,
	NUMBER_OF_ROYALE_AUDIO_SOUNDS
};

enum
{
	_audio_music_drop = 0,
	_audio_music_endgame,
	_audio_music_win,
	NUMBER_OF_ROYALE_AUDIO_MUSIC
};

static char const *const royale_audio_sound_names[NUMBER_OF_ROYALE_AUDIO_SOUNDS] =
{
	"sound\\dialog\\b30\\b30_030_pilot",
	"sound\\dialog\\b30\\b30_032_pilot",
	/* (a squad of two wiped out at once: the closest real line) */
	"sound\\dialog\\multiplayer1\\double_kill",
	/* (two squads left: the last minute's call) */
	"sound\\dialog\\multiplayer1\\one_minute_to_win",
	"sound\\dialog\\multiplayer1\\game_over",
};

static char const *const royale_audio_music_names[NUMBER_OF_ROYALE_AUDIO_MUSIC] =
{
	"levels\\b30\\music\\b30_01",
	"levels\\b30\\music\\b30_05",
	"sound\\sinomatixx_music\\b30_extraction_music",
};

/* the drop's radio: the first line this long into INSERTION, the second
this long after the first ends */
#define ROYALE_AUDIO_RADIO_DELAY_SECONDS 2.0f
#define ROYALE_AUDIO_RADIO_GAP_SECONDS 0.6f
/* the drop's music plays out this long into PLAY */
#define ROYALE_AUDIO_DROP_MUSIC_PLAY_SECONDS 12.0f
/* squads wiped are announced once this few are left (before, too many) */
#define ROYALE_AUDIO_WIPE_ANNOUNCE_SQUADS 8
/* the announcer, the radio and the music's loudness */
#define ROYALE_AUDIO_VOICE_SCALE 1.0f
#define ROYALE_AUDIO_MUSIC_GAIN 0.8f

/* ---------- the module's state */

static struct
{
	boolean started;
	long sounds[NUMBER_OF_ROYALE_AUDIO_SOUNDS];
	long music[NUMBER_OF_ROYALE_AUDIO_MUSIC];
	boolean music_playing[NUMBER_OF_ROYALE_AUDIO_MUSIC];
	/* the last tick's state the cues are edges of */
	enum royale_phase phase;
	long phase_time;
	short squads_alive;
	/* the cues of this phase played (a flag each) */
	unsigned long cues;
	/* (INSERTION) the phase time the second radio line plays at */
	long radio_second_time;
	boolean final_two_announced;
} royale_audio;

enum
{
	_audio_cue_radio_1 = 0,
	_audio_cue_radio_2,
	_audio_cue_drop_music_out,
};

/* ---------- private code */

static long royale_audio_find(
	long group,
	char const *name)
{
	return tag_loaded(group, name);
}

static void royale_audio_play(
	short sound)
{
	long definition_index = royale_audio.sounds[sound];

	if (definition_index == NONE)
		return;
	unspatialized_impulse_sound_new(definition_index, ROYALE_AUDIO_VOICE_SCALE);
	platform_log("royale audio: %s", royale_audio_sound_names[sound]);
}

static void royale_audio_music_start(
	short music)
{
	short other;

	/* (one piece at a time) */
	for (other = 0; other < NUMBER_OF_ROYALE_AUDIO_MUSIC; other++)
	{
		if (other != music && royale_audio.music_playing[other])
		{
			scripted_looping_sound_stop(royale_audio.music[other]);
			royale_audio.music_playing[other] = FALSE;
		}
	}
	if (royale_audio.music[music] == NONE || royale_audio.music_playing[music])
		return;
	scripted_looping_sound_start(royale_audio.music[music], NONE, ROYALE_AUDIO_MUSIC_GAIN);
	royale_audio.music_playing[music] = TRUE;
	platform_log("royale audio: music %s", royale_audio_music_names[music]);
}

static void royale_audio_music_stop(
	short music)
{
	if (!royale_audio.music_playing[music])
		return;
	/* (its stop: the piece's own ending plays out) */
	scripted_looping_sound_stop(royale_audio.music[music]);
	royale_audio.music_playing[music] = FALSE;
}

/* a sound's length in ticks (its longest permutation's) */
static long royale_audio_length_ticks(
	short sound)
{
	long definition_index = royale_audio.sounds[sound];

	if (definition_index == NONE)
		return 0;
	return royale_seconds_to_ticks((real)sound_definition_get(definition_index)->longest_permutation_length / 1000.0f);
}

/* the cue is due this tick: its time reached (since the last tick, in this
phase) and not yet played */
static boolean royale_audio_cue_due(
	short cue,
	long cue_time,
	long phase_time)
{
	if (TEST_FLAG(royale_audio.cues, cue) || phase_time < cue_time)
		return FALSE;
	SET_FLAG(royale_audio.cues, cue, TRUE);
	return TRUE;
}

static void royale_audio_phase_entered(
	enum royale_phase phase)
{
	switch (phase)
	{
	case ROYALE_PHASE_INSERTION:
		royale_audio_music_start(_audio_music_drop);
		break;
	case ROYALE_PHASE_ENDGAME:
		royale_audio_music_start(_audio_music_endgame);
		break;
	case ROYALE_PHASE_RESULTS:
		royale_audio_music_start(_audio_music_win);
		royale_audio_play(_audio_announcer_game_over);
		break;
	default:
		break;
	}
}

static void royale_audio_stop_all(
	void)
{
	short music;

	for (music = 0; music < NUMBER_OF_ROYALE_AUDIO_MUSIC; music++)
		royale_audio_music_stop(music);
}

/* ---------- public code */

void royale_audio_start(
	struct royale_map_config const *config)
{
	short index;
	short missing = 0;

	(void)config;
	royale_audio_stop_all();
	csmemset(&royale_audio, 0, sizeof(royale_audio));
	for (index = 0; index < NUMBER_OF_ROYALE_AUDIO_SOUNDS; index++)
	{
		royale_audio.sounds[index] = royale_audio_find(SOUND_DEFINITION_TAG, royale_audio_sound_names[index]);
		missing += royale_audio.sounds[index] == NONE;
	}
	for (index = 0; index < NUMBER_OF_ROYALE_AUDIO_MUSIC; index++)
	{
		royale_audio.music[index] = royale_audio_find(LOOPING_SOUND_DEFINITION_TAG, royale_audio_music_names[index]);
		missing += royale_audio.music[index] == NONE;
	}
	royale_audio.phase = ROYALE_PHASE_NONE;
	royale_audio.squads_alive = NONE;
	royale_audio.started = TRUE;
	platform_log("royale audio: radio %s, music %s/%s/%s, announcer %s (%d of the sounds missing)",
		royale_audio.sounds[_audio_radio_pilot_1] != NONE ? "yes" : "no",
		royale_audio.music[_audio_music_drop] != NONE ? "yes" : "no",
		royale_audio.music[_audio_music_endgame] != NONE ? "yes" : "no",
		royale_audio.music[_audio_music_win] != NONE ? "yes" : "no",
		royale_audio.sounds[_audio_announcer_game_over] != NONE ? "yes" : "no", missing);
}

void royale_audio_reset(
	void)
{
	/* (the map's sounds are let go with its tags; only forget them) */
	csmemset(&royale_audio, 0, sizeof(royale_audio));
	royale_audio.phase = ROYALE_PHASE_NONE;
}

void royale_audio_tick(
	void)
{
	enum royale_phase phase;
	long phase_time;
	short squads_alive;

	if (!royale_audio.started || !royale_active())
		return;
	phase = royale_phase();
	phase_time = royale_phase_time();
	squads_alive = royale_squads_alive();
	if (phase != royale_audio.phase)
	{
		royale_audio.cues = 0;
		royale_audio.radio_second_time = NONE;
		royale_audio_phase_entered(phase);
	}

	/* the drop: the pilot's radio, then the music plays out once squads land */
	if (phase == ROYALE_PHASE_INSERTION)
	{
		long first = royale_seconds_to_ticks(ROYALE_AUDIO_RADIO_DELAY_SECONDS);

		if (royale_audio_cue_due(_audio_cue_radio_1, first, phase_time))
		{
			royale_audio_play(_audio_radio_pilot_1);
			royale_audio.radio_second_time = first + royale_audio_length_ticks(_audio_radio_pilot_1) +
				royale_seconds_to_ticks(ROYALE_AUDIO_RADIO_GAP_SECONDS);
		}
		if (royale_audio.radio_second_time != NONE &&
			royale_audio_cue_due(_audio_cue_radio_2, royale_audio.radio_second_time, phase_time))
		{
			royale_audio_play(_audio_radio_pilot_2);
		}
	}
	if (phase == ROYALE_PHASE_PLAY &&
		royale_audio_cue_due(_audio_cue_drop_music_out, royale_seconds_to_ticks(ROYALE_AUDIO_DROP_MUSIC_PLAY_SECONDS),
			phase_time))
	{
		royale_audio_music_stop(_audio_music_drop);
	}

	/* the announcer: a squad wiped out (late in the match), the final two */
	if ((phase == ROYALE_PHASE_PLAY || phase == ROYALE_PHASE_ENDGAME) && royale_audio.squads_alive != NONE &&
		squads_alive < royale_audio.squads_alive)
	{
		if (squads_alive == 2 && !royale_audio.final_two_announced)
		{
			royale_audio.final_two_announced = TRUE;
			royale_audio_play(_audio_announcer_final_two);
		}
		else if (squads_alive > 1 && squads_alive <= ROYALE_AUDIO_WIPE_ANNOUNCE_SQUADS)
		{
			royale_audio_play(_audio_announcer_wipe);
		}
	}
	royale_audio.phase = phase;
	royale_audio.phase_time = phase_time;
	royale_audio.squads_alive = squads_alive;
}
