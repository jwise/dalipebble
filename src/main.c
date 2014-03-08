#include <pebble.h>

static Window *window;
static TextLayer *text_layer;

static void handle_tick(struct tm *tm, TimeUnits units_changed) {
  static char s[24];
  
  snprintf(s, sizeof(s), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
  text_layer_set_text(text_layer, s);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  time_t tt;

  text_layer = text_layer_create((GRect) { .origin = { 0, 72 }, .size = { bounds.size.w, 20 } });
  text_layer_set_text(text_layer, "Lel!");
  text_layer_set_text_alignment(text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(text_layer));
  
  tick_timer_service_subscribe(SECOND_UNIT, handle_tick);
  
  time(&tt);
  handle_tick(localtime(&tt), SECOND_UNIT);
}

static void window_unload(Window *window) {
  text_layer_destroy(text_layer);
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
