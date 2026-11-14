/*
ROYALE_CINEMA.C

Royale's presentation moments (royale_cinema.h), drawn from Halo's own
fonts and the director's cameras, as a campaign cutscene would be:

- The flyover: the first seconds of the drop, while the squads ride in from
  past the island's edge (no one can jump yet), a slow letterboxed sweep of
  the island with the match's title and this player's squad.
- The victory: the first seconds of the results, the camera orbiting the
  winning squad, VICTORY for them and DEFEAT with the place for the rest.
- The carnage report: the rest of the results, every squad by its place
  with its players and kills, in the look of Halo's postgame carnage report.
- The Pelicans' bay lit while it carries its squads, so the riders read.

Presentation only: the cameras and the drawing read the synced Royale state
(the phase, its time, the squads, the placements) and change nothing, so
every machine shows the same moment from its own copy of the game.
*/

#include "cseries.h"
#include "camera/observer.h"
#include "cutscene/cinematics.h"
#include "game/players.h"
#include "interface/hud.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "units/units.h"

#include "royale.h"
#include "royale_cinema.h"

#include <math.h>
#include <string.h>

/* ---------- constants */

/* the flyover's length (it ends sooner if the player leaves the Pelican) */
#define CINEMA_FLYOVER_SECONDS 6.5f
/* the victory orbit's length, then the carnage report */
#define CINEMA_ORBIT_SECONDS 7.0f
/* the letterbox's bars, each a share of the window's height, and their
slide in and out */
#define CINEMA_LETTERBOX 0.12f
#define CINEMA_LETTERBOX_SECONDS 0.5f
/* the orbit: its distance, height over the squad, turn rate */
#define CINEMA_ORBIT_DISTANCE 3.6f
#define CINEMA_ORBIT_HEIGHT 1.3f
#define CINEMA_ORBIT_RATE 0.4f
/* the carnage report's rows */
#define CINEMA_REPORT_ROWS 10

#define PELICAN_TAG_NAME "vehicles\\pelican\\pelican"

/* Halo's menu palette (the postgame carnage report's) */
#define CINEMA_PANEL 0xE00A1424
#define CINEMA_PANEL_BORDER 0xFF3F74AE
#define CINEMA_HEADER_BAND 0xFF173257
#define CINEMA_ROW_BAND 0x40203C60
#define CINEMA_OWN_BAND 0x70B88A1E

enum cinema_mode
{
	CINEMA_NONE = 0,
	CINEMA_FLYOVER,
	CINEMA_ORBIT,
	CINEMA_REPORT
};

/* ---------- structures */

/* the director's controls (director.c's struct camera_control): their start */
struct cinema_controls
{
	short local_player_index;
	boolean active;
	byte pad3;
	real seconds_elapsed;
};

/* the camera's data, in the director's (0x40 bytes) */
struct cinema_camera
{
	short local_player_index;
	short mode;
	real clock; /* seconds into the mode's phase, smoothed between ticks */
	real_point3d focus; /* the orbit's, eased after the squad */
	boolean focus_valid;
	byte pad[3];
};

typedef char cinema_camera_size_assert[sizeof(struct cinema_camera) <= 0x40 ? 1 : -1];

/* ---------- globals */

static struct
{
	struct royale_map_config const *config;
	long pelican_definition; /* NONE, or not looked up (-2) */
} cinema = { NULL, -2 };

/* ---------- private code */

static real cinema_phase_seconds(
	void)
{
	return (real)royale_phase_time() / (real)TICKS_PER_SECOND;
}

static long cinema_living_unit(
	long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	struct object_datum *object;

	if (!player || player->unit_index == NONE ||
		!(object = object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit)) ||
		TEST_FLAG(object->object.damage_flags, _object_dead_bit))
	{
		return NONE;
	}
	return player->unit_index;
}

/* the winning squad's middle (its living units'), FALSE if none stands */
static boolean cinema_winners_focus(
	real_point3d *focus)
{
	short squad = royale_winning_squad();
	short seat;
	short count = 0;

	if (squad == NONE)
		return FALSE;
	focus->x = focus->y = focus->z = 0.0f;
	for (seat = 0; seat < ROYALE_SQUAD_SIZE; seat++)
	{
		long unit_index = cinema_living_unit(royale_squad_member(squad, seat));
		real_point3d const *center;

		if (unit_index == NONE)
			continue;
		center = &object_get(unit_index)->object.bounding_sphere_center;
		focus->x += center->x;
		focus->y += center->y;
		focus->z += center->z;
		count++;
	}
	if (!count)
		return FALSE;
	focus->x /= count;
	focus->y /= count;
	focus->z /= count;
	return TRUE;
}

static boolean cinema_bounds_valid(
	void)
{
	return cinema.config && cinema.config->bounds.maximum.x > cinema.config->bounds.minimum.x &&
		cinema.config->bounds.maximum.y > cinema.config->bounds.minimum.y;
}

static enum cinema_mode cinema_mode_of(
	short local_player_index)
{
	long player_index = local_player_get_player_index(local_player_index);

	if (!royale_active() || player_index == NONE || cinematic_in_progress())
		return CINEMA_NONE;
	switch (royale_phase())
	{
	case ROYALE_PHASE_INSERTION:
		if (cinema_bounds_valid() && cinema_phase_seconds() < CINEMA_FLYOVER_SECONDS &&
			royale_player_state(player_index) == ROYALE_PLAYER_IN_PELICAN)
		{
			return CINEMA_FLYOVER;
		}
		return CINEMA_NONE;
	case ROYALE_PHASE_RESULTS:
	{
		real_point3d focus;

		/* (the game's own scoreboard once the results are over: royale.c) */
		if (!royale_game_over() && cinema_phase_seconds() < CINEMA_ORBIT_SECONDS && cinema_winners_focus(&focus))
			return CINEMA_ORBIT;
		return royale_game_over() ? CINEMA_NONE : CINEMA_REPORT;
	}
	default:
		return CINEMA_NONE;
	}
}

static real cinema_smooth(
	real t)
{
	t = PIN(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

/* the flyover's camera at a time: an arc about the island's middle, high
under the drop's height (inside the level), looking down on it */
static void cinema_flyover_camera(
	real seconds,
	real_point3d *position,
	real_vector3d *forward)
{
	struct royale_map_config const *config = cinema.config;
	real t = cinema_smooth(seconds / CINEMA_FLYOVER_SECONDS);
	real center_x = 0.5f * (config->bounds.minimum.x + config->bounds.maximum.x);
	real center_y = 0.5f * (config->bounds.minimum.y + config->bounds.maximum.y);
	real half = 0.5f * MIN(config->bounds.maximum.x - config->bounds.minimum.x,
		config->bounds.maximum.y - config->bounds.minimum.y);
	real ground = config->bounds.minimum.z + 8.0f;
	real top = config->drop_height > ground + 20.0f ? config->drop_height - 10.0f : ground + 40.0f;
	/* (where the arc starts: the match's own, the same on every machine) */
	real start = (real)(royale_match_seed() % 360UL) * (_pi / 180.0f);
	real angle = start + t * (_pi / 3.0f);
	real radius = half * (1.05f - 0.25f * t);
	real_point3d target;

	position->x = center_x + cosf(angle) * radius;
	position->y = center_y + sinf(angle) * radius;
	position->z = top - 10.0f * t;
	/* (the look leads the arc a little) */
	target.x = center_x + cosf(angle + 0.5f) * half * 0.15f;
	target.y = center_y + sinf(angle + 0.5f) * half * 0.15f;
	target.z = ground;
	vector_from_points3d(position, &target, forward);
	if (normalize3d(forward) <= 0.0001f)
		*forward = *global_forward3d;
}

/* the orbit's camera about the squad: out from it, pulled in short of a
wall or the ground between */
static void cinema_orbit_camera(
	real seconds,
	real_point3d const *focus,
	real_point3d *position,
	real_vector3d *forward)
{
	real angle = (real)(royale_winning_squad() * 97 % 360) * (_pi / 180.0f) + seconds * CINEMA_ORBIT_RATE;
	real distance = CINEMA_ORBIT_DISTANCE + 0.6f * (1.0f - cinema_smooth(seconds / 2.0f));
	real_vector3d out;
	real_point3d look = *focus;
	struct collision_result collision;

	out.i = cosf(angle) * distance;
	out.j = sinf(angle) * distance;
	out.k = CINEMA_ORBIT_HEIGHT;
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		focus, &out, NONE, &collision))
	{
		real t = MAX(collision.t - 0.3f / MAX(magnitude3d(&out), 0.01f), 0.15f);

		out.i *= t;
		out.j *= t;
		out.k *= t;
	}
	point_from_line3d(focus, &out, 1.0f, position);
	look.z += 0.2f;
	vector_from_points3d(position, &look, forward);
	if (normalize3d(forward) <= 0.0001f)
		*forward = *global_forward3d;
}

/* ---------- the camera (director.c) */

boolean royale_cinema_camera_wanted(
	short local_player_index)
{
	enum cinema_mode mode = cinema_mode_of(local_player_index);

	return mode == CINEMA_FLYOVER || mode == CINEMA_ORBIT;
}

void royale_cinema_camera_new(
	void *data,
	short local_player_index)
{
	struct cinema_camera *camera = (struct cinema_camera *)data;

	csmemset(camera, 0, sizeof(*camera));
	camera->local_player_index = local_player_index;
	camera->mode = CINEMA_NONE;
}

void royale_cinema_camera_update(
	void *data,
	void *control_data,
	void *result_data)
{
	struct cinema_camera *camera = (struct cinema_camera *)data;
	struct cinema_controls const *controls = (struct cinema_controls const *)control_data;
	struct observer_command *command = (struct observer_command *)result_data;
	enum cinema_mode mode = cinema_mode_of(camera->local_player_index);
	real phase_seconds = cinema_phase_seconds();
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	short parameter;

	if (mode != CINEMA_FLYOVER && mode != CINEMA_ORBIT)
		return;
	/* (the clock runs with the frames, kept to the game's ticks) */
	if (camera->mode != mode)
	{
		camera->mode = (short)mode;
		camera->clock = phase_seconds;
		camera->focus_valid = FALSE;
	}
	else
	{
		camera->clock += controls ? controls->seconds_elapsed : 0.0f;
		if (fabsf(camera->clock - phase_seconds) > 0.15f)
			camera->clock = phase_seconds;
	}
	if (mode == CINEMA_FLYOVER)
		cinema_flyover_camera(camera->clock, &position, &forward);
	else
	{
		real_point3d focus;
		real seconds = controls ? controls->seconds_elapsed : 0.0f;

		if (!cinema_winners_focus(&focus))
			return;
		if (!camera->focus_valid)
		{
			camera->focus = focus;
			camera->focus_valid = TRUE;
		}
		else
		{
			/* (eased after the squad as it moves, a steady frame) */
			real fraction = 1.0f - expf(-seconds / 0.4f);

			camera->focus.x += (focus.x - camera->focus.x) * fraction;
			camera->focus.y += (focus.y - camera->focus.y) * fraction;
			camera->focus.z += (focus.z - camera->focus.z) * fraction;
		}
		cinema_orbit_camera(camera->clock, &camera->focus, &position, &forward);
	}
	observer_up_from_forward(&forward, &up);
	csmemset(command, 0, sizeof(*command));
	command->flags = FLAG(_observer_command_valid_bit);
	command->focus_position = position;
	command->focus_offset = *global_zero_vector3d;
	command->focus_distance = 0.0f;
	command->field_of_view = DEGREES_TO_RADIANS(mode == CINEMA_FLYOVER ? 62.0f : 55.0f);
	command->forward = forward;
	command->up = up;
	command->focus_velocity = *global_zero_vector3d;
	command->timer = 0.0f;
	/* (each frame where it is: no easing behind) */
	for (parameter = 0; parameter < NUMBER_OF_OBSERVER_COMMAND_PARAMETERS; parameter++)
	{
		command->parameter_flags[parameter] = FLAG(_observer_time_valid_bit) | FLAG(_observer_time_force_bit);
		command->parameter_timers[parameter] = 0.0f;
	}
}

/* ---------- the Pelicans' bay */

void royale_cinema_object_lighting(
	long object_index,
	struct render_lighting *lighting)
{
	struct object_datum *object;
	long vehicle_index;
	real const lamp = 0.42f;

	if (!royale_active() || royale_phase() != ROYALE_PHASE_INSERTION || !lighting)
		return;
	if (cinema.pelican_definition == -2)
		cinema.pelican_definition = tag_loaded('vehi', PELICAN_TAG_NAME);
	if (cinema.pelican_definition == NONE || !(object = object_try_and_get(object_index)))
		return;
	/* (the Pelican, or a rider in it, or what a rider carries) */
	vehicle_index = object_index;
	while (vehicle_index != NONE && object_get(vehicle_index)->object.definition_index != cinema.pelican_definition)
		vehicle_index = object_get(vehicle_index)->object.parent_object_index;
	if (vehicle_index == NONE)
		return;
	/* the bay's lamps: a cool light filling the shadows, as the level's
	brightest ambient would */
	lighting->ambient_color.red = MAX(lighting->ambient_color.red, lamp * 0.95f);
	lighting->ambient_color.green = MAX(lighting->ambient_color.green, lamp);
	lighting->ambient_color.blue = MAX(lighting->ambient_color.blue, lamp * 1.1f);
	lighting->shadow_color.red = MAX(lighting->shadow_color.red, lamp * 0.6f);
	lighting->shadow_color.green = MAX(lighting->shadow_color.green, lamp * 0.6f);
	lighting->shadow_color.blue = MAX(lighting->shadow_color.blue, lamp * 0.7f);
}

/* ---------- the drawing */

static long cinema_font(
	boolean large)
{
	long font_index = tag_loaded('font', large ? "ui\\large_ui" : "ui\\small_ui");

	return font_index != NONE ? font_index : hud_get_font_index();
}

static rectangle2d cinema_window(
	void)
{
	rectangle2d window = render.camera.window_bounds;

	offset_rectangle2d(&window, -render.camera.viewport_bounds.x0, -render.camera.viewport_bounds.y0);
	return window;
}

static short cinema_font_height(
	long font_index)
{
	struct font_header *font;

	if (font_index == NONE)
		return 16;
	font = font_definition_get(font_index);
	return (short)MAX(font->ascending_height + font->descending_height + font->leading_height, 8);
}

/* text in bounds (justification 0 left, 1 right, 2 centered), scale times
larger about the bounds' top at its anchor */
static void cinema_text(
	rectangle2d const *bounds,
	long font_index,
	short justification,
	real scale,
	wchar_t const *text,
	real red,
	real green,
	real blue,
	real alpha)
{
	real_argb_color color;
	rectangle2d layout = *bounds;
	real origin_x;

	if (font_index == NONE || alpha <= 0.0f)
		return;
	color.alpha = PIN(alpha, 0.0f, 1.0f);
	color.red = red;
	color.green = green;
	color.blue = blue;
	origin_x = justification == 2 ? 0.5f * (bounds->x0 + bounds->x1) :
		justification == 1 ? (real)bounds->x1 : (real)bounds->x0;
	if (scale != 1.0f)
	{
		/* (laid out in the unscaled units, about the anchor) */
		real half = 0.5f * (bounds->x1 - bounds->x0) / scale;

		if (justification == 2)
		{
			layout.x0 = (short)(origin_x - half);
			layout.x1 = (short)(origin_x + half);
		}
		else if (justification == 1)
			layout.x0 = (short)(origin_x - 2.0f * half);
		else
			layout.x1 = (short)(origin_x + 2.0f * half);
		layout.y1 = (short)(layout.y0 + (bounds->y1 - bounds->y0) / scale);
		rasterizer_text_set_scale(scale, origin_x, (real)bounds->y0);
	}
	draw_string_set_draw_mode(font_index, NONE, justification, 0, &color);
	rasterizer_draw_unicode_string(&layout, NULL, NULL, 0, text);
	if (scale != 1.0f)
		rasterizer_text_set_scale(1.0f, 0.0f, 0.0f);
}

static void cinema_quad(
	short x0,
	short y0,
	short x1,
	short y1,
	pixel32 color)
{
	rectangle2d box;

	if (x1 <= x0 || y1 <= y0)
		return;
	box.x0 = x0;
	box.y0 = y0;
	box.x1 = x1;
	box.y1 = y1;
	draw_quad(&box, color);
}

/* a box's outline, a pixel wide */
static void cinema_frame(
	rectangle2d const *box,
	pixel32 color)
{
	cinema_quad(box->x0, box->y0, box->x1, (short)(box->y0 + 1), color);
	cinema_quad(box->x0, (short)(box->y1 - 1), box->x1, box->y1, color);
	cinema_quad(box->x0, box->y0, (short)(box->x0 + 1), box->y1, color);
	cinema_quad((short)(box->x1 - 1), box->y0, box->x1, box->y1, color);
}

static pixel32 cinema_alpha(
	pixel32 color,
	real alpha)
{
	unsigned long a = (unsigned long)((color >> 24) * PIN(alpha, 0.0f, 1.0f));

	return (pixel32)((color & 0x00FFFFFF) | (a << 24));
}

static void cinema_name(
	long player_index,
	wchar_t *name,
	short count)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	short index;

	for (index = 0; player && index < (short)NUMBEROF(player->name) && index < count - 1 && player->name[index];
		index++)
	{
		name[index] = player->name[index];
	}
	name[index] = 0;
	if (!index && count > 1)
	{
		name[0] = L'-';
		name[1] = 0;
	}
}

static void cinema_squad_names(
	short squad,
	wchar_t *text,
	short count)
{
	wchar_t a[16];
	wchar_t b[16];
	long second = royale_squad_member(squad, 1);

	cinema_name(royale_squad_member(squad, 0), a, NUMBEROF(a));
	if (second == NONE)
		usnprintf(text, count - 1, L"%s", a);
	else
	{
		cinema_name(second, b, NUMBEROF(b));
		usnprintf(text, count - 1, L"%s  &  %s", a, b);
	}
	text[count - 1] = 0;
}

/* the level's name as its players know it */
static wchar_t const *cinema_map_title(
	void)
{
	if (cinema.config && !strcmp(cinema.config->map_name, "b30"))
		return L"THE SILENT CARTOGRAPHER";
	return L"";
}

/* the bars, slid in by amount (0..1) */
static void cinema_letterbox(
	rectangle2d const *window,
	real amount)
{
	short bar = (short)((window->y1 - window->y0) * CINEMA_LETTERBOX * cinema_smooth(amount));

	if (bar <= 0)
		return;
	cinema_quad(window->x0, window->y0, window->x1, (short)(window->y0 + bar), 0xFF000000);
	cinema_quad(window->x0, (short)(window->y1 - bar), window->x1, window->y1, 0xFF000000);
}

static void cinema_draw_flyover(
	long player_index)
{
	rectangle2d window = cinema_window();
	real seconds = cinema_phase_seconds();
	real bars = MIN(seconds, CINEMA_FLYOVER_SECONDS - seconds) / CINEMA_LETTERBOX_SECONDS;
	real fade = PIN((seconds - 0.6f) / 0.8f, 0.0f, 1.0f) * PIN((CINEMA_FLYOVER_SECONDS - 0.4f - seconds) / 0.6f,
		0.0f, 1.0f);
	long large = cinema_font(TRUE);
	long small = cinema_font(FALSE);
	short height = (short)(window.y1 - window.y0);
	short bar = (short)(height * CINEMA_LETTERBOX);
	short line = cinema_font_height(small);
	short squad = royale_squad_of(player_index);
	rectangle2d bounds;
	wchar_t text[96];

	cinema_letterbox(&window, bars);
	/* the title in the upper bar's shadow, the squad in the lower */
	bounds.x0 = window.x0;
	bounds.x1 = window.x1;
	bounds.y0 = (short)(window.y0 + bar + height / 14);
	bounds.y1 = (short)(bounds.y0 + cinema_font_height(large) * 3);
	cinema_text(&bounds, large, 2, 2.4f, L"R O Y A L E", 1.0f, 1.0f, 1.0f, fade);
	bounds.y0 = (short)(bounds.y0 + cinema_font_height(large) * 5 / 2);
	bounds.y1 = (short)(bounds.y0 + line * 2);
	cinema_text(&bounds, small, 2, 1.0f, cinema_map_title(), 0.62f, 0.78f, 1.0f, fade);
	if (squad == NONE)
		return;
	bounds.y1 = (short)(window.y1 - bar - height / 18);
	bounds.y0 = (short)(bounds.y1 - cinema_font_height(large) * 2);
	cinema_squad_names(squad, text, NUMBEROF(text));
	cinema_text(&bounds, large, 2, 1.5f, text, 1.0f, 0.85f, 0.35f, fade);
	bounds.y1 = bounds.y0;
	bounds.y0 = (short)(bounds.y1 - line * 3 / 2);
	usnprintf(text, NUMBEROF(text) - 1, L"YOUR SQUAD  -  %d SQUADS DROP", royale_squad_count());
	text[NUMBEROF(text) - 1] = 0;
	cinema_text(&bounds, small, 2, 1.0f, text, 0.62f, 0.78f, 1.0f, fade);
}

static void cinema_draw_victory(
	long player_index)
{
	rectangle2d window = cinema_window();
	real seconds = cinema_phase_seconds();
	real bars = MIN(seconds, CINEMA_ORBIT_SECONDS - seconds) / CINEMA_LETTERBOX_SECONDS;
	real fade = PIN((seconds - 0.4f) / 0.8f, 0.0f, 1.0f) * PIN((CINEMA_ORBIT_SECONDS - 0.3f - seconds) / 0.6f,
		0.0f, 1.0f);
	long large = cinema_font(TRUE);
	long small = cinema_font(FALSE);
	short height = (short)(window.y1 - window.y0);
	short bar = (short)(height * CINEMA_LETTERBOX);
	short line = cinema_font_height(small);
	short winners = royale_winning_squad();
	short squad = royale_squad_of(player_index);
	boolean won = squad != NONE && squad == winners;
	rectangle2d bounds;
	wchar_t text[96];
	wchar_t names[64];

	cinema_letterbox(&window, bars);
	bounds.x0 = window.x0;
	bounds.x1 = window.x1;
	bounds.y0 = (short)(window.y0 + bar + height / 16);
	bounds.y1 = (short)(bounds.y0 + cinema_font_height(large) * 4);
	if (won)
		cinema_text(&bounds, large, 2, 3.0f, L"V I C T O R Y", 1.0f, 0.85f, 0.35f, fade);
	else if (squad != NONE)
		cinema_text(&bounds, large, 2, 3.0f, L"D E F E A T", 0.85f, 0.25f, 0.2f, fade);
	bounds.y0 = (short)(bounds.y0 + cinema_font_height(large) * 3);
	bounds.y1 = (short)(bounds.y0 + line * 2);
	if (!won && squad != NONE && royale_squad_placement(squad))
	{
		usnprintf(text, NUMBEROF(text) - 1, L"YOUR SQUAD PLACED #%d OF %d", royale_squad_placement(squad),
			royale_squad_count());
		text[NUMBEROF(text) - 1] = 0;
		cinema_text(&bounds, small, 2, 1.0f, text, 0.62f, 0.78f, 1.0f, fade);
	}
	else if (won)
		cinema_text(&bounds, small, 2, 1.0f, L"LAST SQUAD STANDING", 0.62f, 0.78f, 1.0f, fade);
	if (winners == NONE)
		return;
	bounds.y1 = (short)(window.y1 - bar - height / 18);
	bounds.y0 = (short)(bounds.y1 - cinema_font_height(large) * 2);
	cinema_squad_names(winners, names, NUMBEROF(names));
	cinema_text(&bounds, large, 2, 1.5f, names, 1.0f, 0.85f, 0.35f, fade);
	bounds.y1 = bounds.y0;
	bounds.y0 = (short)(bounds.y1 - line * 3 / 2);
	cinema_text(&bounds, small, 2, 1.0f, L"WINNERS", 0.62f, 0.78f, 1.0f, fade);
}

/* the carnage report: Halo's postgame screen's panel, every squad by its
place, this player's squad marked (and shown last if below the rows) */
static void cinema_draw_report(
	long player_index)
{
	rectangle2d window = cinema_window();
	real seconds = cinema_phase_seconds() - CINEMA_ORBIT_SECONDS;
	/* (faded in after the orbit; at once when there was none) */
	real fade = seconds >= 0.0f ? PIN(seconds / 0.5f, 0.0f, 1.0f) : 1.0f;
	long large = cinema_font(TRUE);
	long small = cinema_font(FALSE);
	short line = (short)(cinema_font_height(small) * 5 / 4);
	short title_height = (short)(cinema_font_height(large) * 2);
	short squad_count = royale_squad_count();
	short my_squad = royale_squad_of(player_index);
	short width = (short)MIN(window.x1 - window.x0 - 32, 46 * cinema_font_height(small));
	short left = (short)((window.x0 + window.x1 - width) / 2);
	short rows = (short)MIN(squad_count, CINEMA_REPORT_ROWS);
	short body_rows;
	short top;
	short place_column;
	short squad_column;
	short kills_column;
	rectangle2d panel;
	rectangle2d bounds;
	wchar_t text[128];
	short place;
	short row = 0;

	/* (this player's squad below the rows gets a row of its own) */
	body_rows = rows;
	if (my_squad != NONE && royale_squad_placement(my_squad) > CINEMA_REPORT_ROWS)
		body_rows++;
	top = (short)((window.y0 + window.y1) / 2 - (title_height + line * (body_rows + 4)) / 2);
	panel.x0 = left;
	panel.x1 = (short)(left + width);
	panel.y0 = top;
	panel.y1 = (short)(top + title_height + line * (body_rows + 3));
	/* the screen dimmed behind, the panel and its border */
	cinema_quad(window.x0, window.y0, window.x1, window.y1, cinema_alpha(0x90000000, fade));
	draw_quad(&panel, cinema_alpha(CINEMA_PANEL, fade));
	cinema_frame(&panel, cinema_alpha(CINEMA_PANEL_BORDER, fade));
	/* the title band */
	cinema_quad((short)(panel.x0 + 1), (short)(panel.y0 + 1), (short)(panel.x1 - 1), (short)(panel.y0 + title_height),
		cinema_alpha(CINEMA_HEADER_BAND, fade));
	bounds.x0 = (short)(panel.x0 + line);
	bounds.x1 = (short)(panel.x1 - line);
	bounds.y0 = (short)(panel.y0 + (title_height - cinema_font_height(large)) / 2);
	bounds.y1 = (short)(bounds.y0 + cinema_font_height(large));
	cinema_text(&bounds, large, 0, 1.0f, L"CARNAGE REPORT", 0.62f, 0.8f, 1.0f, fade);
	if (my_squad != NONE && royale_squad_placement(my_squad))
	{
		if (my_squad == royale_winning_squad())
			usnprintf(text, NUMBEROF(text) - 1, L"VICTORY");
		else
			usnprintf(text, NUMBEROF(text) - 1, L"PLACED #%d OF %d", royale_squad_placement(my_squad), squad_count);
	}
	else
		usnprintf(text, NUMBEROF(text) - 1, L"ROYALE");
	text[NUMBEROF(text) - 1] = 0;
	cinema_text(&bounds, large, 1, 1.0f, text, 1.0f, 0.85f, 0.35f, fade);
	/* the columns' headers */
	place_column = (short)(panel.x0 + line);
	squad_column = (short)(panel.x0 + line * 5);
	kills_column = (short)(panel.x1 - line);
	bounds.y0 = (short)(panel.y0 + title_height + line / 2);
	bounds.y1 = (short)(bounds.y0 + line);
	bounds.x0 = place_column;
	bounds.x1 = kills_column;
	cinema_text(&bounds, small, 0, 1.0f, L"PLACE", 0.45f, 0.62f, 0.85f, fade);
	cinema_text(&bounds, small, 1, 1.0f, L"KILLS", 0.45f, 0.62f, 0.85f, fade);
	bounds.x0 = squad_column;
	cinema_text(&bounds, small, 0, 1.0f, L"SQUAD", 0.45f, 0.62f, 0.85f, fade);
	cinema_quad(place_column, (short)(bounds.y1 + 1), kills_column, (short)(bounds.y1 + 2),
		cinema_alpha(CINEMA_PANEL_BORDER, fade));
	/* the squads by their place */
	for (place = 1; place <= squad_count && row < body_rows; place++)
	{
		short squad;

		for (squad = 0; squad < squad_count && row < body_rows; squad++)
		{
			boolean mine = squad == my_squad;
			long a;
			long b;
			short y;
			real red, green, blue;

			if (royale_squad_placement(squad) != place)
				continue;
			/* (past the rows: only this player's squad, in the last) */
			if (row >= rows && !mine)
				continue;
			y = (short)(panel.y0 + title_height + line * (row + 2) - line / 4);
			if (mine)
				cinema_quad((short)(panel.x0 + 2), y, (short)(panel.x1 - 2), (short)(y + line),
					cinema_alpha(CINEMA_OWN_BAND, fade));
			else if (row & 1)
				cinema_quad((short)(panel.x0 + 2), y, (short)(panel.x1 - 2), (short)(y + line),
					cinema_alpha(CINEMA_ROW_BAND, fade));
			red = mine ? 1.0f : 0.85f;
			green = mine ? 0.85f : 0.92f;
			blue = mine ? 0.35f : 1.0f;
			bounds.y0 = (short)(y + line / 8);
			bounds.y1 = (short)(bounds.y0 + line);
			bounds.x0 = place_column;
			bounds.x1 = kills_column;
			usnprintf(text, NUMBEROF(text) - 1, place == 1 ? L"1ST" : place == 2 ? L"2ND" : place == 3 ? L"3RD" :
				L"%dTH", place);
			text[NUMBEROF(text) - 1] = 0;
			cinema_text(&bounds, small, 0, 1.0f, text, red, green, blue, fade);
			a = royale_squad_member(squad, 0);
			b = royale_squad_member(squad, 1);
			usnprintf(text, NUMBEROF(text) - 1, L"%d", royale_player_kills(a) + (b != NONE ? royale_player_kills(b) : 0));
			text[NUMBEROF(text) - 1] = 0;
			cinema_text(&bounds, small, 1, 1.0f, text, red, green, blue, fade);
			bounds.x0 = squad_column;
			bounds.x1 = (short)(kills_column - line * 3);
			cinema_squad_names(squad, text, NUMBEROF(text));
			cinema_text(&bounds, small, 0, 1.0f, text, red, green, blue, fade);
			row++;
		}
	}
}

/* ---------- public code */

void royale_cinema_start(
	struct royale_map_config const *config)
{
	cinema.config = config;
	cinema.pelican_definition = -2;
}

void royale_cinema_reset(
	void)
{
	cinema.config = NULL;
	cinema.pelican_definition = -2;
}

void royale_cinema_tick(
	void)
{
}

boolean royale_cinema_owns_screen(
	short local_player_index)
{
	return cinema_mode_of(local_player_index) != CINEMA_NONE;
}

void royale_cinema_draw(
	short local_player_index)
{
	long player_index = local_player_get_player_index(local_player_index);

	switch (cinema_mode_of(local_player_index))
	{
	case CINEMA_FLYOVER:
		cinema_draw_flyover(player_index);
		break;
	case CINEMA_ORBIT:
		cinema_draw_victory(player_index);
		break;
	case CINEMA_REPORT:
		cinema_draw_report(player_index);
		break;
	default:
		break;
	}
}
