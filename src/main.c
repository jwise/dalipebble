/* Dali Clock - a melting digital clock for Pebble.
 * Copyright (c) 2014 Joshua Wise <joshua@joshuawise.com>
 * Copyright (c) 1991-2010 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 */

#include <pebble.h>

#include "numbers.h"

/* If you want to burn battery, STYLE_DEBUG Dalis the seconds across the
 * screen.  STYLE_HOURSMINUTES is something you might actually want to use. 
 * Once the OS allows watchfaces to grab the back button, it seems like the
 * right thing is to add support for the back button to show seconds.
 */

// #define STYLE_DEBUG
#define STYLE_HOURSMINUTES
#define ANIMATION_TIME_MSEC 1000

/**************************************************************************/
/* Cross-chunk globals. */

/* The namespace pollution here is pretty bad, but oh well. */

/* Renamed from 'struct frame' to avoid confusion with the PebbleOS 'Frame'. */
struct dali_frame;

static POS char_height, char_width, colon_width;
static Window *mainwin;

static struct dali_frame *base_frames[12];
static struct dali_frame *temp_frame;
static struct dali_frame *clear_frame;

static signed char current_digits[6];
static signed char target_digits[6];

/* The real 'abort()' does not work on Pebble, even though it exists. */
static void _abort() {
  *(volatile int *)0;
}


/**************************************************************************/
/* Scanline parsing. 
 *
 * (Largely stolen from the PalmOS original).
 */

/* Internally, the application converts the bitmaps to lists of scanlines;
 * each scanline has segments that we then lerp between.  If we wanted to
 * save RAM, it would probably be a good idea to pre-parse into scanlines at
 * compile time, so we don't have to have both the parsed version and the
 * pixmap version resident in RAM.  */
#define MAX_SEGS_PER_LINE 3

#undef countof
#define countof(x) (sizeof((x))/sizeof(*(x)))

struct scanline {
  POS left[MAX_SEGS_PER_LINE], right[MAX_SEGS_PER_LINE];
};

struct dali_frame {
  struct scanline scanlines[1];
};

static struct dali_frame *frame_mk(int width, int height) {
  struct dali_frame *fr = calloc(1, sizeof (struct dali_frame) + (sizeof (struct scanline) * (height - 1)));
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

static struct dali_frame *frame_from_pixmap(const unsigned char *bits, int width, int height) {
  int x, y;
  struct dali_frame *frame;
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

static void frame_copy(struct dali_frame *from, struct dali_frame *to) {
  int y;

  for (y = 0; y < char_height; y++)
    to->scanlines[y] = from->scanlines[y];  /* copies the whole struct */
}

static void numbers_init() {
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


static void numbers_free() {
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

static void frame_lerp(int from, int to, int tm /* 0 to 65535. */) {
  int y, x;
  struct dali_frame *fromf, *tof;
  
  fromf = from >= 0 ? base_frames[from] : clear_frame;
  tof   = to   >= 0 ? base_frames[to]   : clear_frame;
  
#define LERP(a, b) (((a) * (65536 - tm) + (b) * (tm + 1)) / 65536)
  for (y = 0; y < char_height; y++)
    for (x = 0; x < MAX_SEGS_PER_LINE; x++) {
      temp_frame->scanlines[y].left [x] = LERP(fromf->scanlines[y].left [x], tof->scanlines[y].left [x]);
      temp_frame->scanlines[y].right[x] = LERP(fromf->scanlines[y].right[x], tof->scanlines[y].right[x]);
    }
#undef LERP
}


/**************************************************************************/
/* PebbleOS drawing routines. */

static void draw_horizontal_line(GContext *ctx, int x1, int x2, int y, int screen_width, bool black_p) {
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

static void frame_render(struct dali_frame *frame, GContext *ctx, GPoint ofs) {
  int px, py;
  GRect bbox = layer_get_bounds(window_get_root_layer(mainwin));

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


/**************************************************************************/
/* PebbleOS main loop. */

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
static struct tm curtm; /* Make sure that the seconds counter is in sync with the Dali-ing. */
static int needs_animate = 1;
static uint32_t animtime;

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
  frame_lerp(current_digits[4] == -1 ? -1 : 10, 10, animtime);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = 0 });
  x += colon_width;
  
  frame_lerp(current_digits[4], target_digits[4], animtime);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = 0 });
  x += char_width;
  
  frame_lerp(current_digits[5], target_digits[5], animtime);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = 0 });

#elif defined(STYLE_HOURSMINUTES)
  int x;
  
  /* Hours... */
  x = (bbox.size.w - char_width * 2) / 2;
  frame_lerp(current_digits[0], target_digits[0], animtime);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = 0 });
  x += char_width;
  
  frame_lerp(current_digits[1], target_digits[1], animtime);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = 0 });
  x += char_width;
  
  /* Minutes ... */
  x = (bbox.size.w - char_width * 2) / 2;
  frame_lerp(current_digits[2], target_digits[2], animtime);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = char_height + 8 });
  x += char_width;
  
  frame_lerp(current_digits[3], target_digits[3], animtime);
  frame_render(temp_frame, ctx, (GPoint) { .x = x, .y = char_height + 8 });
  x += char_width;
  
  snprintf(s, sizeof(s),
           "%04d-%02d-%02d",
           curtm.tm_year + 1900, curtm.tm_mon + 1, curtm.tm_mday);
  graphics_draw_text(ctx, s, 
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     (GRect) { .origin = { 0, bbox.size.h - 18 - 1 - 14 }, .size = { bbox.size.w, 28 } },
                     GTextOverflowModeFill,
                     GTextAlignmentCenter,
                     NULL
                     );

  if (clock_is_24h_style()) {
    snprintf(s, sizeof(s),
             "%02d:%02d:%02d",
             curtm.tm_hour, curtm.tm_min, curtm.tm_sec);
  } else {
    int h = curtm.tm_hour;
    if (h > 12) h -= 12;
    if (h == 0) h = 12;
    snprintf(s, sizeof(s),
             "%d:%02d:%02d %s",
             h, curtm.tm_min, curtm.tm_sec,
             curtm.tm_hour < 12 ? "AM" : "PM");
  }
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
  layer_mark_dirty(window_get_root_layer(mainwin));
}

static AnimationImplementation anim_impl = {
  .setup = NULL,
  .update = handle_anim,
  .teardown = NULL,
};

static void handle_tick(struct tm *_tm, TimeUnits units_changed) {
  int i;
  struct tm *tm;
  time_t tt;
  
  /* Hah!  Pebble's mktime() is actually totally busted (if you invoke it,
   * it crashes in the midst of 'validate_structure()', when it is doing
   * some manipulations on tm_mon ...  even if the tm_mon you pass in is
   * totally legitimate).  So if we want to look one second into the future
   * to see when to animate, we have to get our own time_t, rather than
   * rebuilding one from the tm that was passed in.
   *
   * While I'm complaining -- is there some good reason that I'm not seeing
   * for why the POSIX time API is the way it is?  Why are pointers being
   * thrown around willy-nilly?
   */
  
  time(&tt);
  tm = localtime(&tt);
  
  for (i = 0; i < 6; i++)
    current_digits[i] = target_digits[i];

  memcpy(&curtm, tm, sizeof(*tm));
  
  /* To see what we're animating *to*, we look a second into the future. */
  tt++;
  tm = localtime(&tt);
  
  if (clock_is_24h_style()) {
    target_digits[0] = tm->tm_hour / 10;
    target_digits[1] = tm->tm_hour % 10;
  } else {
    int h = tm->tm_hour;
    if (h > 12) h -= 12;
    if (h == 0) h = 12;
    
    /* Smoothly animating this left to right would be cooler, but people who
     * use 12 hour time suck anyway.  */
    target_digits[0] = h / 10;
    if (target_digits[0] == 0) target_digits[0] = -1;
    
    target_digits[1] = h % 10;
  }
    
  target_digits[2] = tm->tm_min / 10;
  target_digits[3] = tm->tm_min % 10;
  target_digits[4] = tm->tm_sec / 10;
  target_digits[5] = tm->tm_sec % 10;
  
#ifdef STYLE_HOURSMINUTES
  if (tm->tm_sec == 0 || needs_animate) {
    animation_schedule(transition_anim);
    needs_animate = 0;
  } else
    layer_mark_dirty(window_get_root_layer(mainwin));
#else
  animation_schedule(transition_anim);
#endif
}

static void window_load(Window *mainwin) {
  Layer *window_layer = window_get_root_layer(mainwin);
  time_t tt;
  struct tm *tm;
  
  layer_set_update_proc(window_layer, update_layer);
  tick_timer_service_subscribe(SECOND_UNIT, handle_tick);
  
  transition_anim = animation_create();
  animation_set_duration(transition_anim, 1000);
  animation_set_implementation(transition_anim, &anim_impl);
  needs_animate = 1; /* Make sure to kick one off next time. */
  
  time(&tt);
  tm = localtime(&tt);
  memcpy(&curtm, tm, sizeof(*tm));
  
  animtime = 0;
}

static void window_unload(Window *mainwin) {
  animation_destroy(transition_anim);
  tick_timer_service_unsubscribe();
}

static void init(void) {
  mainwin = window_create();
  window_set_window_handlers(mainwin, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  const bool animated = true;
  window_stack_push(mainwin, animated);
  
  numbers_init();
}

static void deinit(void) {
  numbers_free();
  window_destroy(mainwin);
}

int main(void) {
  init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Dali Clock for Pebble initialized");
  
  app_event_loop();
  deinit();
}
