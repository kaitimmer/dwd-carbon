/**
 * Cloud layer
 *
 * @author    Kai Timmer
 * @copyright 2026 Kai Timmer
 * @license   https://www.gnu.org/licenses/gpl-3.0.html GPL-3.0-or-later
 * @link      https://github.com/kaitimmer/dwd-carbon
 */

#include "cloud_layer.h"
#include "graph_common.h"
#include <stddef.h>
#include <stdlib.h>

#define CLEAR_THRESHOLD 15

struct CloudLayer {
	Layer *layer;
	uint8_t cover[GRAPH_HOURS];
	uint8_t cover_low[GRAPH_HOURS];
	uint8_t cover_mid[GRAPH_HOURS];
	uint8_t cover_high[GRAPH_HOURS];
	bool has_layers; // false: draw the total only
	uint8_t hourly_code[GRAPH_HOURS];
	uint8_t current_hour;
};

static void prv_draw_cloud(GContext *ctx, int cx, int cy, int r) {
	graphics_fill_circle(ctx, GPoint(cx, cy), r);
	graphics_fill_circle(ctx, GPoint(cx - r, cy + r / 2), r * 2 / 3);
	graphics_fill_circle(ctx, GPoint(cx + r, cy + r / 2), r * 2 / 3);
}

static void prv_set_cloud_fill(GContext *ctx, uint8_t code) {
#if defined(PBL_COLOR)
	// Color clouds based on WMO severity for severe conditions only
	GColor cloud_color;
	if (code == 95 || code == 96 || code == 99) {
		cloud_color = GColorLightGray; // storm clouds — grey
	} else if (code == 75 || code == 77 || code == 85 || code == 86) {
		cloud_color = GColorCeleste; // blizzard clouds — light blue
	} else {
		cloud_color = GColorWhite;
	}
	graphics_context_set_fill_color(ctx, cloud_color);
#else
	(void)code;
	graphics_context_set_fill_color(ctx, GColorWhite);
#endif
}

// Draw one row of clouds. Later hours first so sooner clouds overlap them.
static void prv_draw_row(GContext *ctx, CloudLayer *cl, const uint8_t *cover,
                         int graph_x, int graph_w, int cy, int r_large) {
	for (int i = GRAPH_HOURS - 1; i >= 0; i--) {
		if (cover[i] < CLEAR_THRESHOLD)
			continue;
		prv_set_cloud_fill(ctx, cl->hourly_code[i]);
		int cx = graph_x + (long)(i * 2 + 1) * graph_w / (GRAPH_HOURS * 2);
		if (cover[i] < 50) {
			// Light cover: a plain dot; a 1px-radius cloud renders as a "+".
			graphics_fill_rect(ctx, GRect(cx - 1, cy - 1, 3, 3), 0, GCornerNone);
		} else {
			prv_draw_cloud(ctx, cx, cy, r_large);
		}
	}
}

static void prv_update_proc(Layer *layer, GContext *ctx) {
	CloudLayer *cl = *(CloudLayer **)layer_get_data(layer);
	GRect bounds = layer_get_bounds(layer);
	int graph_x = GRAPH_OFFSET_X;
	int graph_w = bounds.size.w - graph_x;
	int cy = bounds.size.h / 2;

	graphics_context_set_antialiased(ctx, false);

	if (cl->has_layers) {
		// High/mid/low rows stacked top to bottom; rows touch at most 1px.
		int pitch = (bounds.size.h - 1) / 3;
		int r_large = pitch / 2;
		int cy_high = r_large;
		prv_draw_row(ctx, cl, cl->cover_high, graph_x, graph_w, cy_high, r_large);
		prv_draw_row(ctx, cl, cl->cover_mid, graph_x, graph_w,
		             cy_high + pitch, r_large);
		prv_draw_row(ctx, cl, cl->cover_low, graph_x, graph_w,
		             cy_high + 2 * pitch, r_large);
		return;
	}

	for (int i = GRAPH_HOURS - 1; i >= 0; i--) {
		if (cl->cover[i] < CLEAR_THRESHOLD)
			continue;

		prv_set_cloud_fill(ctx, cl->hourly_code[i]);

		// Draw later hours first so sooner clouds overlap them.
		int cx = graph_x + (long)(i * 2 + 1) * graph_w / (GRAPH_HOURS * 2);
		int r;
		if (cl->cover[i] < 40) {
			r = 2;
		} else if (cl->cover[i] < 70) {
			r = 3;
		} else {
			r = 4;
		}
		prv_draw_cloud(ctx, cx, cy, r);
	}
}

CloudLayer *cloud_layer_create(GRect frame) {
	CloudLayer *cl = malloc(sizeof(CloudLayer));
	if (!cl)
		return NULL;
	memset(cl->cover, 0, sizeof(cl->cover));
	memset(cl->cover_low, 0, sizeof(cl->cover_low));
	memset(cl->cover_mid, 0, sizeof(cl->cover_mid));
	memset(cl->cover_high, 0, sizeof(cl->cover_high));
	cl->has_layers = false;
	memset(cl->hourly_code, 0, sizeof(cl->hourly_code));
	cl->current_hour = 0;

	cl->layer = layer_create_with_data(frame, sizeof(CloudLayer *));
	*(CloudLayer **)layer_get_data(cl->layer) = cl;
	layer_set_update_proc(cl->layer, prv_update_proc);
	return cl;
}

void cloud_layer_destroy(CloudLayer *layer) {
	if (!layer)
		return;
	layer_destroy(layer->layer);
	free(layer);
}

Layer *cloud_layer_get_layer(CloudLayer *layer) {
	return layer ? layer->layer : NULL;
}

void cloud_layer_set_data(CloudLayer *layer, const uint8_t cover[24],
                          const uint8_t cover_low[24],
                          const uint8_t cover_mid[24],
                          const uint8_t cover_high[24],
                          const uint8_t hourly_code[24], uint8_t current_hour) {
	if (!layer)
		return;
	memcpy(layer->cover, cover, GRAPH_HOURS);
	memcpy(layer->cover_low, cover_low, GRAPH_HOURS);
	memcpy(layer->cover_mid, cover_mid, GRAPH_HOURS);
	memcpy(layer->cover_high, cover_high, GRAPH_HOURS);
	layer->has_layers = false;
	for (int i = 0; i < GRAPH_HOURS; i++) {
		if (cover_low[i] || cover_mid[i] || cover_high[i])
			layer->has_layers = true;
	}
	memcpy(layer->hourly_code, hourly_code, GRAPH_HOURS);
	layer->current_hour = current_hour;
	layer_mark_dirty(layer->layer);
}
