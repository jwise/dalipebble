#include <pebble.h>

#include "numbers.h"

/* Just so we all have this straight, I have:
 *   a window load handler, that schedules
 *   a timer handler, that schedules an animation, which
 *   periodically invokes an animation handler, which marks a layer as dirty, which
 *   eventually causes the layer to be updated.
 *
 * Four layers of callbacks.
 *
 * Don't you love PebbleOS?
 */

static Window *window;
static Animation *transition_anim;
static uint32_t animtime;

static void update_layer(struct Layer *layer, GContext *ctx) {
  char s[24];
  time_t tt;
  struct tm *tm;
  GRect bbox = layer_get_bounds(layer);
  
  time(&tt);
  tm = localtime(&tt);
  
  snprintf(s, sizeof(s), "%02d:%02d:%02d %04x", tm->tm_hour, tm->tm_min, tm->tm_sec, (unsigned int)animtime);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx,
                     (GRect) { .origin = { 0, 72 }, .size = { bbox.size.w, 20 } },
                     0, GCornerNone);
  graphics_draw_text(ctx, s, 
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     (GRect) { .origin = { 0, 72 }, .size = { bbox.size.w, 20 } },
                     GTextOverflowModeFill,
                     GTextAlignmentCenter,
                     NULL
                     );
}

static void handle_anim(Animation *anim, const uint32_t _animtime /* between 0 and 65536 */) {
  animtime = _animtime;
  layer_mark_dirty(window_get_root_layer(window));
}

static AnimationImplementation anim_impl = {
  .setup = NULL,
  .update = handle_anim,
  .teardown = NULL,
};

static void handle_tick(struct tm *tm, TimeUnits units_changed) {
  animation_schedule(transition_anim);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  
  layer_set_update_proc(window_layer, update_layer);
  tick_timer_service_subscribe(SECOND_UNIT, handle_tick);
  
  transition_anim = animation_create();
  animation_set_duration(transition_anim, 250);
  animation_set_implementation(transition_anim, &anim_impl);
  
  animtime = 0;
}

static void window_unload(Window *window) {
  animation_destroy(transition_anim);
  tick_timer_service_unsubscribe();
}

static void init(void) {
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  const bool animated = true;
  window_stack_push(window, animated);
}

static void deinit(void) {
  window_destroy(window);
}

int main(void) {
  init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);
  
  app_event_loop();
  deinit();
}
