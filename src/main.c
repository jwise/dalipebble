#include <pebble.h>

#include "numbers.h"

// #define STYLE_DEBUG
#define STYLE_HOURSMINUTES
#define ANIMATION_TIME_MSEC 1000

/* Globals. */
struct scanline;
struct frame;

POS char_height, char_width, colon_width;
static Window *window;

struct frame *base_frames[12];
struct frame *temp_frame;
struct frame *clear_frame;

signed char current_digits[6];
signed char target_digits[6];

static uint32_t animtime;

void _abort() {
  *(volatile int *)0;
}


/**************************************************************************/
/* Scanline parsing. */

/* Internally, the application converts the bitmaps to lists of scanlines;
 * each scanline has segments that we then lerp between.  */
#define MAX_SEGS_PER_LINE 3

#undef countof
#define countof(x) (sizeof((x))/sizeof(*(x)))

struct scanline {
  POS left[MAX_SEGS_PER_LINE], right[MAX_SEGS_PER_LINE];
};

struct frame {
  struct scanline scanlines[1];
};

static struct frame *frame_mk(int width, int height) {
  struct frame *fr = calloc(1, sizeof (struct frame) + (sizeof (struct scanline) * (height - 1)));
  int x, y;
  
  if (!fr) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "out of memory for frame of size (%d,%d)!\n", width, height);
    _abort();
  }
  
  for (y = 0; y < height; y++)
    for (x = 0; x < MAX_SEGS_PER_LINE; x++)
      fr->scanlines[y].left[x] = fr->scanlines[y].right[x] = width / 2;
  
  return fr;
}

static struct frame *frame_from_pixmap(const unsigned char *bits, int width, int height)
{
  int x, y;
  struct frame *frame;
  POS *left, *right;

  frame = frame_mk(width, height);

  for (y = 0; y < height; y++)
    {
      int seg, end;
      x = 0;
# define GETBIT(bits,x,y) \
         (!! ((bits) [((y) * ((width+7) >> 3)) + ((x) >> 3)] \
              & (1 << ((x) & 7))))

      left = frame->scanlines[y].left;
      right = frame->scanlines[y].right;

      for (seg = 0; seg < MAX_SEGS_PER_LINE; seg++)
        left [seg] = right [seg] = width / 2;

      for (seg = 0; seg < MAX_SEGS_PER_LINE; seg++)
        {
          for (; x < width; x++)
            if (GETBIT (bits, x, y)) break;
          if (x == width) break;
          left [seg] = x;
          for (; x < width; x++)
            if (! GETBIT (bits, x, y)) break;
          right [seg] = x;
        }

      for (; x < width; x++)
        if (GETBIT (bits, x, y))
          {
            /* This means the font is too curvy.  Increase MAX_SEGS_PER_LINE
               and recompile. */
            APP_LOG(APP_LOG_LEVEL_ERROR, "builtin font is bogus");
            _abort();
          }

      /* If there were any segments on this line, then replicate the last
         one out to the end of the line.  If it's blank, leave it alone,
         meaning it will be a 0-pixel-wide line down the middle.
       */
      end = seg;
      if (end > 0)
        for (; seg < MAX_SEGS_PER_LINE; seg++)
          {
            left [seg] = left [end-1];
            right [seg] = right [end-1];
          }

# undef GETBIT
    }

  return frame;
}

static void frame_copy(struct frame *from, struct frame *to) {
  int y;

  for (y = 0; y < char_height; y++)
    to->scanlines[y] = from->scanlines[y];  /* copies the whole struct */
}

static void numbers_init()
{
  unsigned int i;
  /* We've pre-chosen the font needed to make this fit. */
  const struct raw_number *raw = get_raw_numbers();

  char_width  = raw[0].width;
  char_height = raw[0].height;
  colon_width = raw[10].width;

  for (i = 0; i < countof(base_frames); i++)
    base_frames[i] =
      frame_from_pixmap(raw[i].bits, raw[i].width, raw[i].height);
  temp_frame = frame_mk(char_width, char_height);
  clear_frame = frame_mk(char_width, char_height);

  for (i = 0; i < countof(target_digits); i++)
    target_digits[i] = current_digits[i] = -1;
}


static void numbers_free()
{
  unsigned int i;
# define FREEIF(x) do { if ((x)) { free((x)); (x) = 0; } } while (0)
# define FREELOOP(x) do { \
    for (i = 0; i < countof ((x)); i++) FREEIF ((x)[i]); } while (0)

  FREELOOP(base_frames);
  FREEIF  (temp_frame);
  FREEIF  (clear_frame);

# undef FREELOOP
# undef FREEIF
}

static void draw_horizontal_line(GContext *ctx, int x1, int x2, int y, int screen_width, bool black_p)
{
  if (x1 > screen_width) x1 = screen_width;
  else if (x1 < 0) x1 = 0;

  if (x2 > screen_width) x2 = screen_width;
  else if (x2 < 0) x2 = 0;

  if (x1 == x2) return;

  if (x1 > x2)
    {
      uint16_t swap = x1;
      x1 = x2;
      x2 = swap;
    }

  graphics_context_set_stroke_color(ctx, black_p ? GColorBlack : GColorWhite);
  graphics_draw_line(ctx, (GPoint) { .x = x1, .y = y }, (GPoint) { .x = x2, .y = y });
}

static void frame_render(struct frame *frame, GContext *ctx, GPoint ofs) {
  int px, py;
  GRect bbox = layer_get_bounds(window_get_root_layer(window));

  for (py = 0; py < char_height; py++)
    {
      struct scanline *line = &frame->scanlines [py];
      int last_right = 0;

      for (px = 0; px < MAX_SEGS_PER_LINE; px++)
        {
          if (px > 0 &&
              (line->left[px] == line->right[px] ||
               (line->left [px] == line->left [px-1] &&
                line->right[px] == line->right[px-1])))
            continue;

          /* Erase the line between the last segment and this segment.
           */
          draw_horizontal_line (ctx, 
                                ofs.x + last_right,
                                ofs.x + line->left [px],
                                ofs.y + py,
                                bbox.size.w, 1 /* Black */);

          /* Draw the line of this segment.
           */
          draw_horizontal_line (ctx, 
                                ofs.x + line->left [px],
                                ofs.x + line->right[px],
                                ofs.y + py,
                                bbox.size.w, 0 /* White */);

          last_right = line->right[px];
        }

      /* Erase the line between the last segment and the right edge.
       */
      draw_horizontal_line (ctx,
                            ofs.x + last_right,
                            ofs.x + char_width,
                            ofs.y + py,
                            bbox.size.w, 1 /* Black */);
    }
}

static void frame_lerp(int from, int to) {
  int y, x;
  struct frame *fromf, *tof;
  
  fromf = from >= 0 ? base_frames[from] : clear_frame;
  tof   = to   >= 0 ? base_frames[to]   : clear_frame;
  
#define LERP(a, b) (((a) * (65536 - animtime) + (b) * (animtime + 1)) / 65536)
  for (y = 0; y < char_height; y++)
    for (x = 0; x < MAX_SEGS_PER_LINE; x++) {
      temp_frame->scanlines[y].left [x] = LERP(fromf->scanlines[y].left [x], tof->scanlines[y].left [x]);
      temp_frame->scanlines[y].right[x] = LERP(fromf->scanlines[y].right[x], tof->scanlines[y].right[x]);
    }
}

/**************************************************************************/
/* PebbleOS shim. */

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

static Animation *transition_anim;
static struct tm curtm, lasttm; /* Delay by one, so we tick over from 59 to 00. */
int needs_animate = 1;

static void update_layer(struct Layer *layer, GContext *ctx) {
  char s[64];
  GRect bbox = layer_get_bounds(layer);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx, GColorBlack);
  
  graphics_fill_rect(ctx,
                     (GRect) { .origin = { 0, 0 }, .size = { bbox.size.w, bbox.size.h } },
                     0, GCornerNone);

#if defined(STYLE_DEBUG)
  int x;

  snprintf(s, sizeof(s), "%02d:%02d:%02d %04x", lasttm.tm_hour, lasttm.tm_min, lasttm.tm_sec, (unsigned int)animtime);
  graphics_draw_text(ctx, s, 
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     (GRect) { .origin = { 0, 72 }, .size = { bbox.size.w, 20 } },
                     GTextOverflowModeFill,
                     GTextAlignmentCenter,
                     NULL
                     );
  
  int x;
  
  x = 0;
  frame_lerp(current_digits[4] == -1 ? -1 : 10, 10);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = 0 });
  x += colon_width;
  
  frame_lerp(current_digits[4], target_digits[4]);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = 0 });
  x += char_width;
  
  frame_lerp(current_digits[5], target_digits[5]);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = 0 });

#elif defined(STYLE_HOURSMINUTES)
  int x;
  
  /* Hours... */
  x = (bbox.size.w - char_width * 2) / 2;
  frame_lerp(current_digits[0], target_digits[0]);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = 0 });
  x += char_width;
  
  frame_lerp(current_digits[1], target_digits[1]);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = 0 });
  x += char_width;
  
  /* Minutes ... */
  x = (bbox.size.w - char_width * 2) / 2;
  frame_lerp(current_digits[2], target_digits[2]);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = char_height + 8 });
  x += char_width;
  
  frame_lerp(current_digits[3], target_digits[3]);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = char_height + 8 });
  x += char_width;
  
  snprintf(s, sizeof(s),
           "%04d-%02d-%02d",
           lasttm.tm_year + 1900, lasttm.tm_mon + 1, lasttm.tm_mday);
  graphics_draw_text(ctx, s, 
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     (GRect) { .origin = { 0, bbox.size.h - 18 - 1 - 14 }, .size = { bbox.size.w, 28 } },
                     GTextOverflowModeFill,
                     GTextAlignmentCenter,
                     NULL
                     );


  snprintf(s, sizeof(s),
           "%02d:%02d:%02d",
           lasttm.tm_hour, lasttm.tm_min, lasttm.tm_sec);
           
  graphics_draw_text(ctx, s, 
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     (GRect) { .origin = { 0, bbox.size.h - 14 }, .size = { bbox.size.w, 28 } },
                     GTextOverflowModeFill,
                     GTextAlignmentCenter,
                     NULL
                     );
  
  
#endif

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
  int i;
  
  for (i = 0; i < 6; i++)
    current_digits[i] = target_digits[i];

  target_digits[0] = tm->tm_hour / 10;
  target_digits[1] = tm->tm_hour % 10;
  target_digits[2] = tm->tm_min / 10;
  target_digits[3] = tm->tm_min % 10;
  target_digits[4] = tm->tm_sec / 10;
  target_digits[5] = tm->tm_sec % 10;
  
  memcpy(&lasttm, &curtm, sizeof(*tm));
  memcpy(&curtm, tm, sizeof(*tm));
  
#ifdef STYLE_HOURSMINUTES
  if (tm->tm_sec == 0 || needs_animate) {
    animation_schedule(transition_anim);
    needs_animate = 0;
  } else
    layer_mark_dirty(window_get_root_layer(window));
#else
  animation_schedule(transition_anim);
#endif
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  time_t tt;
  struct tm *tm;
  
  layer_set_update_proc(window_layer, update_layer);
  tick_timer_service_subscribe(SECOND_UNIT, handle_tick);
  
  transition_anim = animation_create();
  animation_set_duration(transition_anim, 1000);
  animation_set_implementation(transition_anim, &anim_impl);
  needs_animate = 1;
  
  time(&tt);
  tm = localtime(&tt);
  memcpy(&lasttm, tm, sizeof(*tm));
  memcpy(&curtm, tm, sizeof(*tm));
  
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
  
  numbers_init();
}

static void deinit(void) {
  numbers_free();
  window_destroy(window);
}

int main(void) {
  init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);
  
  app_event_loop();
  deinit();
}
