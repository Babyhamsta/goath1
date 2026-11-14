/* royale_fx.c: Royale's world effects: the Covenant ship over the beam, the rumble, orange sky and shake near the beam, the supply pods' fiery fall

All of it the map's own tags (b30's), and presentation only: each machine
draws and plays its own from the synced Royale state (the phase, the beam's
side and front, the match clock the pods fall by), so every player sees the
same thing; nothing here changes a rule or an object.

- The ship: a Covenant dropship's model (vehicles\c_dropship\c_dropship),
  drawn huge and high over the beam's front, along it, hanging where the
  beam comes down (from the drop on, at the side it starts from).
- Near the beam: the sky goes orange (a glow from the top of the screen
  down, stronger the nearer the front) and the view shakes (the falling
  damage effect's camera shake, without its flash, in pulses). The deep
  rumble is the beam's own (royale_beam.c: the Covenant dropship's engine
  loop, sound\sfx\vehicles\covydrop, louder as the front nears).
- The supply pods: each falls burning (the rocket's exhaust particles and a
  trail of fire and smoke behind it, weapons\rocket launcher\exhaust's
  material) and lands with a boom (the Pelican's explosion particles and
  the fuel rod gun's explosion sound) that shakes the screens near it. */

#include "cseries.h"
#include "bitmaps/bitmap_color_conversion.h"
#include "bitmaps/bitmap_group.h"
#include "cache/texture_cache.h"
#include "effects/contrail_definitions.h"
#include "effects/particle_system_definitions.h"
#include "effects/particle_systems.h"
#include "effects/player_effects.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "math/real_math.h"
#include "models/model_definitions.h"
#include "models/models.h"
#include "objects/damage.h"
#include "objects/damage_effect_definitions.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "sound/game_sound.h"
#include "sound/sound_definitions.h"
#include "tag_files/tag_groups.h"
#include "units/vehicle_definitions.h"
#include "royale_fx.h"
#include "royale.h"
#include "royale_beam.h"
#include "royale_loot.h"

#include <math.h>
#include <string.h>

void platform_log(char const *format, ...);
void draw_quad(rectangle2d const *bounds, pixel32 color);

/* ---------- constants */

/* the ship: its length a share of the front's, its height over the play
area's top, how far over the glassed side of the front it hangs */
#define ROYALE_FX_SHIP_LENGTH_SHARE 0.7f
#define ROYALE_FX_SHIP_HEIGHT 150.0f
#define ROYALE_FX_SHIP_BEHIND 25.0f

/* the orange sky and the shake: from this far ahead of the front (world
units), full at it */
#define ROYALE_FX_NEAR_DISTANCE 90.0f
#define ROYALE_FX_SHAKE_DISTANCE 45.0f
#define ROYALE_FX_SHAKE_PERIOD_SECONDS 0.5f
#define ROYALE_FX_SHAKE_SCALE 0.35f
#define ROYALE_FX_SKY_ALPHA 0.42f
#define ROYALE_FX_SKY_BANDS 24
/* how far past the HUD's bounds the screen's edge is (royale_beam.c's) */
#define ROYALE_FX_HUD_EDGE 0.087f

/* the supply pods' fall (royale_loot.c's ROYALE_LOOT_POD_FALL_SECONDS and
_HEIGHT: the loot's crate falls so) */
#define ROYALE_FX_POD_FALL_SECONDS 5.0f
#define ROYALE_FX_POD_FALL_HEIGHT 120.0f
#define ROYALE_FX_POD_TRAIL_LENGTH 45.0f
#define ROYALE_FX_POD_TRAIL_POINTS 16
/* the boom's shake reaches this far */
#define ROYALE_FX_POD_BOOM_DISTANCE 60.0f

/* ---------- the tags */

static char const *const royale_fx_ship_name = "vehicles\\c_dropship\\c_dropship";
static char const *const royale_fx_trail_names[] =
{
	"weapons\\rocket launcher\\exhaust",
	"vehicles\\banshee\\banshee wingtip contrail",
	"weapons\\fuel rod gun\\tail",
};
static char const *const royale_fx_flame_names[] =
{
	"weapons\\rocket launcher\\effects\\rocket exhaust",
	"weapons\\frag grenade\\effects\\explosion med",
};
static char const *const royale_fx_boom_names[] =
{
	"vehicles\\pelican\\effects\\explosion_pelican",
	"effects\\particle systems\\explosion large",
};
static char const *const royale_fx_boom_sound_names[] =
{
	"sound\\sfx\\weapons\\fuel rod gun\\explosion",
	"sound\\sfx\\impulse\\impacts\\elite_sword_boom",
};
static char const *const royale_fx_shake_names[] =
{
	"globals\\falling",
	"globals\\distance",
};

/* ---------- the module's state */

struct royale_fx_vertex
{
	real_point3d point;
	real_point2d texture;
	pixel32 color;
};

struct royale_fx_strip
{
	struct royale_fx_vertex vertices[2 * ROYALE_FX_POD_TRAIL_POINTS];
	short pairs;
	real_point3d centroid;
};

static struct
{
	boolean started;
	struct royale_bounds bounds;
	real_point3d pod_spots[ROYALE_MAXIMUM_POD_SPOTS];
	short pod_spot_count;
	long ticks;
	/* the tags (NONE: the map has none) */
	long ship_model;
	real ship_radius;
	long trail;
	long flame;
	long boom;
	long boom_sound;
	long shake;
	/* each pod: falling, its height above the ground (for the drawing),
	and its boom played */
	struct
	{
		boolean falling;
		boolean landed;
		real_point3d position;
	} pods[ROYALE_LOOT_POD_COUNT];
} royale_fx;

/* ---------- private code */

static long royale_fx_first_tag(
	long group,
	char const *const *names,
	short count)
{
	short index;

	for (index = 0; index < count; index++)
	{
		long tag_index = tag_loaded(group, names[index]);

		if (tag_index != NONE)
			return tag_index;
	}
	return NONE;
}

static real royale_fx_seconds(
	void)
{
	long per_second = MAX(royale_seconds_to_ticks(1.0f), 1);

	return (real)royale_fx.ticks / (real)per_second;
}

static boolean royale_fx_world_phase(
	void)
{
	enum royale_phase phase = royale_phase();

	return phase == ROYALE_PHASE_INSERTION || phase == ROYALE_PHASE_PLAY || phase == ROYALE_PHASE_ENDGAME;
}

/* the beam's front line (its two ends across the map) and the way to
safety, from the beam's side and front (royale_beam.c's
royale_beam_front_line, on the config's bounds) */
static boolean royale_fx_front_line(
	real_point2d *a,
	real_point2d *b,
	real_vector2d *safe)
{
	struct royale_bounds const *bounds = &royale_fx.bounds;
	short side = royale_beam_side();
	real front = royale_beam_front();
	real length;

	if (side == NONE || bounds->maximum.x <= bounds->minimum.x || bounds->maximum.y <= bounds->minimum.y)
		return FALSE;
	switch (side)
	{
	case ROYALE_SIDE_NORTH:
		length = bounds->maximum.y - bounds->minimum.y;
		a->x = bounds->minimum.x; b->x = bounds->maximum.x;
		a->y = b->y = bounds->maximum.y - front * length;
		safe->i = 0.0f; safe->j = -1.0f;
		break;
	case ROYALE_SIDE_EAST:
		length = bounds->maximum.x - bounds->minimum.x;
		a->y = bounds->minimum.y; b->y = bounds->maximum.y;
		a->x = b->x = bounds->maximum.x - front * length;
		safe->i = -1.0f; safe->j = 0.0f;
		break;
	case ROYALE_SIDE_SOUTH:
		length = bounds->maximum.y - bounds->minimum.y;
		a->x = bounds->minimum.x; b->x = bounds->maximum.x;
		a->y = b->y = bounds->minimum.y + front * length;
		safe->i = 0.0f; safe->j = 1.0f;
		break;
	default:
		length = bounds->maximum.x - bounds->minimum.x;
		a->y = bounds->minimum.y; b->y = bounds->maximum.y;
		a->x = b->x = bounds->minimum.x + front * length;
		safe->i = 1.0f; safe->j = 0.0f;
		break;
	}
	return TRUE;
}

/* how far a point is ahead of the front (world units; 0 or less: at it or
behind it); a large number when no beam runs */
static real royale_fx_distance_ahead(
	real_point3d const *point)
{
	short side = royale_beam_side();
	real depth;

	if (side == NONE || royale_beam_front() <= 0.0f)
		return 1.0e9f;
	depth = royale_beam_depth(point);
	return depth <= -1.0e8f ? 1.0e9f : -depth;
}

/* the local player's unit's position, for a player standing in the world */
static boolean royale_fx_local_unit_position(
	short local_player_index,
	real_point3d *position)
{
	long player_index = local_player_get_player_index(local_player_index);
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	enum royale_player_state state;

	if (!player || player->unit_index == NONE || !object_header_try_and_get(player->unit_index))
		return FALSE;
	state = royale_player_state(player_index);
	if (state != ROYALE_PLAYER_ALIVE && state != ROYALE_PLAYER_DOWNED && state != ROYALE_PLAYER_FALLING)
		return FALSE;
	object_get_origin(player->unit_index, position);
	return TRUE;
}

/* a shake on a local player's screen (the damage effect's camera shake and
rumble, without its flash) */
static void royale_fx_shake(
	long player_index,
	real scale)
{
	struct damage_data damage;
	struct damage_effect_definition *definition;
	struct screen_flash_definition flash;

	if (royale_fx.shake == NONE || scale <= 0.0f)
		return;
	definition = damage_effect_definition_get(royale_fx.shake);
	flash = definition->screen_flash;
	damage_data_new(&damage, royale_fx.shake);
	csmemset(&definition->screen_flash, 0, sizeof(definition->screen_flash));
	player_effect_start(player_index, &damage, global_down3d, scale, 0.0f);
	definition->screen_flash = flash;
}

/* each local player near the front: a shake now and then */
static void royale_fx_beam_shake_tick(
	void)
{
	long period = MAX(royale_seconds_to_ticks(ROYALE_FX_SHAKE_PERIOD_SECONDS), 1);
	short local_player_index;

	if (royale_fx.ticks % period != 0 || royale_beam_front() <= 0.0f)
		return;
	for (local_player_index = local_player_get_next(NONE); local_player_index != NONE;
		local_player_index = local_player_get_next(local_player_index))
	{
		real_point3d position;
		real ahead;

		if (!royale_fx_local_unit_position(local_player_index, &position))
			continue;
		ahead = royale_fx_distance_ahead(&position);
		if (ahead < ROYALE_FX_SHAKE_DISTANCE)
		{
			real near = ahead <= 0.0f ? 1.0f : 1.0f - ahead / ROYALE_FX_SHAKE_DISTANCE;

			royale_fx_shake(local_player_get_player_index(local_player_index), ROYALE_FX_SHAKE_SCALE * near * near);
		}
	}
}

static void royale_fx_pod_boom(
	real_point3d const *ground)
{
	real_point3d point = *ground;
	short local_player_index;

	point.z += 1.0f;
	if (royale_fx.boom != NONE)
	{
		real_argb_color color = { 1.0f, 1.0f, 0.7f, 0.35f };

		particle_system_new_unattached(royale_fx.boom, &point, global_zero_vector3d, &color, 2.0f);
	}
	if (royale_fx.boom_sound != NONE)
	{
		struct sound_location location;

		location.position = point;
		location.forward = *global_up3d;
		location.translational_velocity = *global_zero_vector3d;
		scenario_location_from_point(&location.game_location, &point);
		unattached_impulse_sound_new(royale_fx.boom_sound, &location, 1.0f);
	}
	for (local_player_index = local_player_get_next(NONE); local_player_index != NONE;
		local_player_index = local_player_get_next(local_player_index))
	{
		real_point3d position;
		real distance;

		if (!royale_fx_local_unit_position(local_player_index, &position))
			continue;
		distance = sqrtf(distance_squared3d(&position, &point));
		if (distance < ROYALE_FX_POD_BOOM_DISTANCE)
		{
			royale_fx_shake(local_player_get_player_index(local_player_index),
				0.8f * (1.0f - distance / ROYALE_FX_POD_BOOM_DISTANCE));
		}
	}
}

/* each pod as the loot's crate falls (royale_loot.c, from the same match
clock): fire about it falling, a boom as it lands */
static void royale_fx_pods_tick(
	void)
{
	struct royale_loot_plan const *plan = royale_loot_plan_get();
	enum royale_phase phase = royale_phase();
	long now = royale_match_time();
	long endgame_start = phase == ROYALE_PHASE_ENDGAME ? now - royale_phase_time() : NONE;
	long fall_ticks = MAX(royale_seconds_to_ticks(ROYALE_FX_POD_FALL_SECONDS), 1);
	long flame_period = MAX(royale_seconds_to_ticks(0.05f), 1);
	short index;

	if (!plan || (phase != ROYALE_PHASE_PLAY && phase != ROYALE_PHASE_ENDGAME))
		return;
	for (index = 0; index < plan->pod_count && index < ROYALE_LOOT_POD_COUNT; index++)
	{
		struct royale_loot_pod_plan const *pod = &plan->pods[index];
		long fall_start = royale_loot_pod_fall_start(royale_play_start_time(), endgame_start,
			royale_seconds_to_ticks((real)pod->drop_second));
		real_point3d ground;
		long elapsed;
		real fraction;

		if (pod->spot < 0 || pod->spot >= royale_fx.pod_spot_count || royale_fx.pods[index].landed)
			continue;
		if (fall_start == NONE || now < fall_start)
			continue;
		ground = royale_fx.pod_spots[pod->spot];
		elapsed = now - fall_start;
		fraction = MIN((real)elapsed / (real)fall_ticks, 1.0f);
		royale_fx.pods[index].position = ground;
		royale_fx.pods[index].position.z = ground.z + ROYALE_FX_POD_FALL_HEIGHT * (1.0f - fraction * fraction);
		if (fraction < 1.0f)
		{
			royale_fx.pods[index].falling = TRUE;
			if (royale_fx.flame != NONE && elapsed % flame_period == 0)
			{
				/* (orange fire, a little behind it) */
				real_argb_color color = { 1.0f, 1.0f, 0.55f, 0.15f };
				real_point3d point = royale_fx.pods[index].position;

				point.z += 1.5f;
				particle_system_new_unattached(royale_fx.flame, &point, global_zero_vector3d, &color, 2.5f);
			}
			continue;
		}
		royale_fx.pods[index].falling = FALSE;
		royale_fx.pods[index].landed = TRUE;
		/* (one that landed long before this machine began to watch: no boom) */
		if (elapsed <= fall_ticks + royale_seconds_to_ticks(1.0f))
			royale_fx_pod_boom(&ground);
		platform_log("royale fx: supply pod %d boom", index);
	}
}

/* ---------- the world's drawing */

static pixel32 royale_fx_pixel(
	real red,
	real green,
	real blue,
	real alpha)
{
	real_argb_color color;

	/* (additive materials take no alpha: their colors are faded too) */
	color.alpha = PIN(alpha, 0.0f, 1.0f);
	color.red = PIN(red * color.alpha, 0.0f, 1.0f);
	color.green = PIN(green * color.alpha, 0.0f, 1.0f);
	color.blue = PIN(blue * color.alpha, 0.0f, 1.0f);
	return real_argb_color_to_pixel32(&color);
}

static void royale_fx_strip_add(
	struct royale_fx_strip *strip,
	real_point3d const *p0,
	real_point3d const *p1,
	real u,
	pixel32 color)
{
	struct royale_fx_vertex *vertices;

	if (strip->pairs >= ROYALE_FX_POD_TRAIL_POINTS)
		return;
	vertices = &strip->vertices[2 * strip->pairs];
	vertices[0].point = *p0;
	vertices[0].texture.x = u;
	vertices[0].texture.y = 1.0f;
	vertices[0].color = color;
	vertices[1].point = *p1;
	vertices[1].texture.x = u;
	vertices[1].texture.y = 0.0f;
	vertices[1].color = color;
	strip->centroid.x += 0.5f * (p0->x + p1->x);
	strip->centroid.y += 0.5f * (p0->y + p1->y);
	strip->centroid.z += 0.5f * (p0->z + p1->z);
	strip->pairs++;
}

/* a strip of quads through the trail's contrail material (royale_beam.c's
way) */
static void royale_fx_strip_draw(
	struct royale_fx_strip *strip)
{
	struct contrail_definition *definition;
	struct bitmap_data *bitmap;
	short segments = (short)(strip->pairs - 1);
	short triangle_count = (short)(2 * segments);
	long triangle_buffer;
	long vertex_buffer;

	if (strip->pairs < 2 || royale_fx.trail == NONE)
		return;
	definition = contrail_definition_get(royale_fx.trail);
	bitmap = bitmap_group_get_bitmap_from_sequence(definition->bitmap.index, definition->first_sequence_index, 0);
	if (!bitmap)
		return;
	rasterizer_globals.current_lock_operation = _rasterizer_lock_contrail;
	if (_texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE))
	{
		triangle_buffer = rasterizer_dynamic_triangles_new(triangle_count);
		vertex_buffer = rasterizer_dynamic_vertices_new(6, 2 * strip->pairs);
		if (triangle_buffer != NONE && vertex_buffer != NONE)
		{
			short *triangles = rasterizer_dynamic_triangles_lock(triangle_buffer);
			struct royale_fx_vertex *vertices = rasterizer_dynamic_vertices_lock(vertex_buffer);
			short segment;

			if (triangles && vertices)
			{
				csmemcpy(vertices, strip->vertices, sizeof(struct royale_fx_vertex) * 2 * strip->pairs);
				for (segment = 0; segment < segments; segment++)
				{
					triangles[0] = (short)(2 * segment);
					triangles[1] = (short)(2 * segment + 1);
					triangles[2] = (short)(2 * segment + 2);
					triangles[3] = (short)(2 * segment + 2);
					triangles[4] = (short)(2 * segment + 1);
					triangles[5] = (short)(2 * segment + 3);
					triangles += 6;
				}
			}
			rasterizer_dynamic_triangles_unlock(triangle_buffer);
			rasterizer_dynamic_vertices_unlock(vertex_buffer);
			strip->centroid.x /= strip->pairs;
			strip->centroid.y /= strip->pairs;
			strip->centroid.z /= strip->pairs;
			rasterizer_dynamic_unlit_geometry_draw((struct shader const *)&definition->shader, bitmap, 0,
				triangle_buffer, vertex_buffer, triangle_count, &strip->centroid, 0);
		}
		if (triangle_buffer != NONE)
			rasterizer_dynamic_triangles_delete(triangle_buffer);
		if (vertex_buffer != NONE)
			rasterizer_dynamic_vertices_delete(vertex_buffer);
	}
	rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
}

/* a falling pod's trail: fire at the pod, smoke above it, facing the
camera */
static void royale_fx_render_pod_trail(
	real_point3d const *pod,
	real seconds)
{
	static struct royale_fx_strip strip;
	short layer;
	real_vector2d to_camera, across;
	real length;

	to_camera.i = render.camera.position.x - pod->x;
	to_camera.j = render.camera.position.y - pod->y;
	length = sqrtf(to_camera.i * to_camera.i + to_camera.j * to_camera.j);
	if (length < 0.01f)
		return;
	across.i = -to_camera.j / length;
	across.j = to_camera.i / length;
	for (layer = 0; layer < 2; layer++)
	{
		/* (a wide orange-to-smoke plume, a white-hot core) */
		static real const widths[2] = { 7.0f, 2.2f };
		short point;

		csmemset(&strip, 0, sizeof(strip));
		for (point = 0; point < ROYALE_FX_POD_TRAIL_POINTS; point++)
		{
			real t = (real)point / (real)(ROYALE_FX_POD_TRAIL_POINTS - 1);
			real width = widths[layer] * (0.6f + 1.4f * t) * (layer ? 1.0f - t : 1.0f);
			real flicker = 0.85f + 0.15f * sinf(seconds * 31.0f + t * 9.0f + (real)layer);
			real_point3d p0, p1;
			pixel32 color = layer ?
				royale_fx_pixel(1.0f, 0.9f, 0.6f, (1.0f - t) * flicker) :
				royale_fx_pixel(1.0f, 0.45f - 0.25f * t, 0.12f, (1.0f - t * 0.9f) * 0.85f * flicker);

			p0.x = pod->x - across.i * width * 0.5f;
			p0.y = pod->y - across.j * width * 0.5f;
			p0.z = pod->z + t * ROYALE_FX_POD_TRAIL_LENGTH;
			p1.x = pod->x + across.i * width * 0.5f;
			p1.y = pod->y + across.j * width * 0.5f;
			p1.z = p0.z;
			royale_fx_strip_add(&strip, &p0, &p1, t * 3.0f + seconds * 4.0f, color);
		}
		royale_fx_strip_draw(&strip);
	}
}

/* the Covenant ship hanging over the beam's front, along it */
static void royale_fx_render_ship(
	real seconds)
{
	real_matrix4x3 node_matrices[MAXIMUM_NODES_PER_MODEL];
	real function_values[NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS];
	struct render_lighting lighting;
	struct model *model;
	real_point2d a, b;
	real_vector2d safe;
	real_point3d position;
	real_vector3d forward, up;
	real line_length, scale;
	short node_index, function_index;

	if (royale_fx.ship_model == NONE || !royale_fx_front_line(&a, &b, &safe))
		return;
	model = model_definition_get(royale_fx.ship_model);
	if (model->nodes.count <= 0 || model->nodes.count > MAXIMUM_NODES_PER_MODEL)
		return;
	line_length = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
	if (line_length < 1.0f)
		return;
	/* (over the middle of the front, a little over the glassed side, slowly
	swaying) */
	position.x = 0.5f * (a.x + b.x) - safe.i * ROYALE_FX_SHIP_BEHIND;
	position.y = 0.5f * (a.y + b.y) - safe.j * ROYALE_FX_SHIP_BEHIND;
	position.z = royale_fx.bounds.maximum.z + ROYALE_FX_SHIP_HEIGHT + 4.0f * sinf(seconds * 0.35f);
	forward.i = (b.x - a.x) / line_length;
	forward.j = (b.y - a.y) / line_length;
	forward.k = 0.0f;
	up = *global_up3d;
	scale = ROYALE_FX_SHIP_LENGTH_SHARE * 0.5f * line_length / MAX(royale_fx.ship_radius, 1.0f);
	model_get_node_matrices(model, node_matrices, &position, &forward, &up);
	for (node_index = 0; node_index < model->nodes.count; node_index++)
	{
		real_point3d *point = &node_matrices[node_index].position;

		node_matrices[node_index].scale *= scale;
		point->x = position.x + (point->x - position.x) * scale;
		point->y = position.y + (point->y - position.y) * scale;
		point->z = position.z + (point->z - position.z) * scale;
	}
	for (function_index = 0; function_index < NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS; function_index++)
		function_values[function_index] = 1.0f;
	/* (lit from above by the sky, and from below by the beam's fire) */
	csmemset(&lighting, 0, sizeof(lighting));
	lighting.ambient_color.red = 0.42f;
	lighting.ambient_color.green = 0.36f;
	lighting.ambient_color.blue = 0.45f;
	lighting.distant_light_count = 2;
	lighting.distant_lights[0].color.red = 0.75f;
	lighting.distant_lights[0].color.green = 0.75f;
	lighting.distant_lights[0].color.blue = 0.8f;
	lighting.distant_lights[0].direction.i = 0.3f;
	lighting.distant_lights[0].direction.j = 0.2f;
	lighting.distant_lights[0].direction.k = -0.93f;
	lighting.distant_lights[1].color.red = 1.0f;
	lighting.distant_lights[1].color.green = 0.45f;
	lighting.distant_lights[1].color.blue = 0.12f;
	lighting.distant_lights[1].direction.i = 0.0f;
	lighting.distant_lights[1].direction.j = 0.0f;
	lighting.distant_lights[1].direction.k = 1.0f;
	lighting.shadow_vector = *global_down3d;
	rasterizer_models_begin(TRUE);
	render_model(royale_fx.ship_model, 10000.0f, node_matrices, NULL, NULL, function_values, &lighting,
		&position, royale_fx.ship_radius * scale, NULL, NONE, 0, 0);
	rasterizer_models_end();
}

/* ---------- public code */

void royale_fx_start(
	struct royale_map_config const *config)
{
	long ship_definition;

	csmemset(&royale_fx, 0, sizeof(royale_fx));
	royale_fx.ship_model = NONE;
	if (!config)
		return;
	royale_fx.bounds = config->bounds;
	royale_fx.pod_spot_count = (short)PIN(config->pod_spot_count, 0, ROYALE_MAXIMUM_POD_SPOTS);
	csmemcpy(royale_fx.pod_spots, config->pod_spots, sizeof(royale_fx.pod_spots[0]) * royale_fx.pod_spot_count);
	ship_definition = tag_loaded(VEHICLE_DEFINITION_TAG, royale_fx_ship_name);
	if (ship_definition != NONE)
	{
		struct object_definition *definition = object_definition_get(ship_definition);

		royale_fx.ship_model = definition->object.model.index;
		royale_fx.ship_radius = definition->object.bounding_radius;
	}
	royale_fx.trail = royale_fx_first_tag(CONTRAIL_DEFINITION_TAG, royale_fx_trail_names,
		(short)NUMBEROF(royale_fx_trail_names));
	royale_fx.flame = royale_fx_first_tag(PARTICLE_SYSTEM_DEFINITION_TAG, royale_fx_flame_names,
		(short)NUMBEROF(royale_fx_flame_names));
	royale_fx.boom = royale_fx_first_tag(PARTICLE_SYSTEM_DEFINITION_TAG, royale_fx_boom_names,
		(short)NUMBEROF(royale_fx_boom_names));
	royale_fx.boom_sound = royale_fx_first_tag(SOUND_DEFINITION_TAG, royale_fx_boom_sound_names,
		(short)NUMBEROF(royale_fx_boom_sound_names));
	royale_fx.shake = royale_fx_first_tag(DAMAGE_EFFECT_DEFINITION_TAG, royale_fx_shake_names,
		(short)NUMBEROF(royale_fx_shake_names));
	royale_fx.started = TRUE;
	platform_log("royale fx: ship %s (radius %.1f), trail %s, flame %s, boom %s/%s, shake %s",
		royale_fx.ship_model != NONE ? "yes" : "no", royale_fx.ship_radius, royale_fx.trail != NONE ? "yes" : "no",
		royale_fx.flame != NONE ? "yes" : "no", royale_fx.boom != NONE ? "yes" : "no",
		royale_fx.boom_sound != NONE ? "yes" : "no", royale_fx.shake != NONE ? "yes" : "no");
}

void royale_fx_reset(
	void)
{
	csmemset(&royale_fx, 0, sizeof(royale_fx));
	royale_fx.ship_model = NONE;
}

void royale_fx_tick(
	void)
{
	if (!royale_fx.started || !royale_active())
		return;
	royale_fx.ticks++;
	if (!royale_fx_world_phase())
		return;
	royale_fx_pods_tick();
	royale_fx_beam_shake_tick();
}

/* the sky going orange near the beam: a glow from the top of the view
down, stronger the nearer the front */
void royale_fx_draw(
	short local_player_index)
{
	rectangle2d bounds, screen;
	real_point3d position;
	real ahead, strength, seconds;
	short width, height, band;

	if (!royale_fx.started || !royale_active() || !royale_fx_world_phase())
		return;
	if (!royale_fx_local_unit_position(local_player_index, &position))
		return;
	ahead = royale_fx_distance_ahead(&position);
	if (ahead >= ROYALE_FX_NEAR_DISTANCE)
		return;
	strength = ahead <= 0.0f ? 1.0f : 1.0f - ahead / ROYALE_FX_NEAR_DISTANCE;
	strength *= strength;
	seconds = royale_fx_seconds();
	strength *= 0.9f + 0.1f * sinf(seconds * 1.7f);
	bounds = render.camera.window_bounds;
	offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
	width = (short)(bounds.x1 - bounds.x0);
	height = (short)(bounds.y1 - bounds.y0);
	screen.x0 = (short)(bounds.x0 - width * ROYALE_FX_HUD_EDGE);
	screen.x1 = (short)(bounds.x1 + width * ROYALE_FX_HUD_EDGE);
	screen.y0 = (short)(bounds.y0 - height * ROYALE_FX_HUD_EDGE);
	screen.y1 = (short)(bounds.y1 + height * ROYALE_FX_HUD_EDGE);
	for (band = 0; band < ROYALE_FX_SKY_BANDS; band++)
	{
		/* (the top 70% of the view, fading down) */
		real t = (real)band / (real)ROYALE_FX_SKY_BANDS;
		real fade = 1.0f - t;
		real alpha = ROYALE_FX_SKY_ALPHA * strength * fade * fade;
		short span = (short)(screen.y1 - screen.y0);
		rectangle2d rows;
		pixel32 color;

		rows.x0 = screen.x0;
		rows.x1 = screen.x1;
		rows.y0 = (short)(screen.y0 + span * 0.7f * t);
		rows.y1 = (short)(screen.y0 + span * 0.7f * (t + 1.0f / ROYALE_FX_SKY_BANDS) + 1);
		color = ((pixel32)(PIN(alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24) |
			((pixel32)255 << 16) | ((pixel32)(110 + 40 * t) << 8) | (pixel32)30;
		draw_quad(&rows, color);
	}
}

void royale_fx_render(
	void)
{
	real seconds;
	short index;

	/* (Royale's alone: the render calls it in every game) */
	if (!royale_fx.started || !royale_active() || !royale_fx_world_phase())
		return;
	seconds = royale_fx_seconds();
	royale_fx_render_ship(seconds);
	for (index = 0; index < ROYALE_LOOT_POD_COUNT; index++)
	{
		if (royale_fx.pods[index].falling)
			royale_fx_render_pod_trail(&royale_fx.pods[index].position, seconds);
	}
}
