#include <stdio.h>
#include <time.h>
#include "my_ui.h"
#include "lvgl/lvgl.h"

#define POWER_MAX_W 3200
#define QUARTER_S   900
#define SOLAR_MAX_W 3200   

#define PURPLE      0xf52ad3
#define GREEN       0x00ff00
#define CLOCK_CLR   0xf4700b
#define CON_CLR     0x00ffff

#define X_OFFSET    400

#define Y_OFFSET_1_R  -160
#define Y_OFFSET_2_R  -110
#define Y_OFFSET_3_R  -30
#define Y_OFFSET_4_R  20
#define Y_OFFSET_5_R 220

#define Y_OFFSET_1_L  -160
#define Y_OFFSET_2_L  -110
#define Y_OFFSET_3_L  -30
#define Y_OFFSET_4_L  20
#define Y_OFFSET_5_L 90
#define Y_OFFSET_6_L  140
#define Y_OFFSET_7_L 220

#define ARC_SIZE_L  500
#define ARC_SIZE_S  450
#define ARC_WIDTH    10

static lv_obj_t *arc_grid_pow;
static lv_obj_t *arc_cur_peak;
static lv_obj_t *arc_15_min;
static lv_obj_t *arc_solar;
static lv_obj_t *arc_used_pow;

static lv_obj_t *lbl_grid_power;
static lv_obj_t *lbl_cur_peak;
static lv_obj_t *lbl_mon_peak;
static lv_obj_t *lbl_15_min;
static lv_obj_t *lbl_solar;
static lv_obj_t *lbl_solar_today;
static lv_obj_t *lbl_clock;
static lv_obj_t *lbl_weather;
static lv_obj_t *lbl_used_pow;

static lv_obj_t *lbl_grid_power_t;
static lv_obj_t *lbl_cur_peak_t;
static lv_obj_t *lbl_mon_peak_t;
static lv_obj_t *lbl_solar_t;
static lv_obj_t *lbl_used_pow_t;

static bool first_run = true;

void animate_arc_to_zero(lv_obj_t *arc, int delay_ms);

extern const char *weather_description(int weather_code);

void my_ui_set_clock(void)
{
    if (lbl_clock == NULL) return;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    lv_label_set_text_fmt(lbl_clock, "%02d:%02d:%02d",tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

void my_ui_set_grid_pow(int watts, bool valid)
{
    if (lbl_grid_power == NULL || arc_grid_pow == NULL) return;

    if (!valid) {
        lv_label_set_text(lbl_grid_power, "--");
        lv_arc_set_value(arc_grid_pow, 0);
        return;
    }

    lv_label_set_text_fmt(lbl_grid_power, "%d", watts);

    /* Clamp to the arc range */
    int v = watts;
    if (v > POWER_MAX_W) v = POWER_MAX_W;
    if (v < -POWER_MAX_W) v = -POWER_MAX_W;
    lv_arc_set_value(arc_grid_pow, -v);

    /* Positive = import, negative = export */
    lv_color_t c = (watts >= 0) ? lv_color_hex(PURPLE) : lv_color_hex(GREEN);
    lv_obj_set_style_arc_color(arc_grid_pow, c, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(lbl_grid_power, c, 0);
    lv_obj_set_style_text_color(lbl_grid_power_t, c, 0);
}

void my_ui_set_used_pow(int watts, bool valid)
{
    if (lbl_used_pow == NULL || arc_used_pow == NULL) return;

    if (!valid) {
        lv_label_set_text(lbl_used_pow, "0");
        lv_arc_set_value(arc_used_pow, 0);
        return;
    }

    int value = watts;
    if (value < 0) value = 0;
    if (value > POWER_MAX_W) value = POWER_MAX_W;

    lv_arc_set_value(arc_used_pow, value);
    lv_label_set_text_fmt(lbl_used_pow, "%d", watts);
}

void my_ui_set_mon_peak(int watts, bool valid)
{
    if (lbl_mon_peak == NULL) return;

    if (!valid) {
        lv_label_set_text(lbl_mon_peak, "--");
        return;
    }
    lv_label_set_text_fmt(lbl_mon_peak, "%4d", watts);
}


void my_ui_set_cur_peak(int energy_wh, int max_demand_w, int elapsed_s, bool valid)
{
    if (arc_cur_peak == NULL || lbl_cur_peak == NULL) return;

    if (elapsed_s < 0) elapsed_s = 0;
    if (elapsed_s > QUARTER_S) elapsed_s = QUARTER_S;
   if (elapsed_s == 0) {
        first_run = false;
        animate_arc_to_zero(arc_cur_peak, 2000);
        animate_arc_to_zero(arc_15_min, 2000);
    }
   
    if (arc_15_min) lv_arc_set_value(arc_15_min, elapsed_s);
    

    if (first_run || !valid) {
        lv_arc_set_value(arc_cur_peak, 0);
        lv_obj_set_style_arc_color(arc_cur_peak, lv_color_hex(0x2090E0), LV_PART_INDICATOR);
        lv_obj_set_style_text_color(lbl_cur_peak, lv_palette_main(LV_PALETTE_CYAN), 0);
        lv_obj_set_style_text_color(lbl_cur_peak_t, lv_palette_main(LV_PALETTE_CYAN), 0);
        lv_label_set_text(lbl_cur_peak, "---");

        int left = QUARTER_S - elapsed_s;
        lv_label_set_text_fmt(lbl_15_min, "left: %d:%02d", left / 60, left % 60);
        return;
    }
    
    energy_wh = energy_wh * 4; // modify energy_wh to represent the total energy in Wh for the current quarter-hour
    lv_arc_set_value(arc_cur_peak, energy_wh );

    int maxP = max_demand_w  < 2500 ? 2500 : max_demand_w;
    lv_arc_set_range(arc_cur_peak, 0, maxP ); 
    //printf("energy_wh=%d max_demand_w=%d elapsed_s=%d maxP=%d  %d   %d \n", energy_wh, max_demand_w, elapsed_s, maxP, energy_wh * QUARTER_S, maxP * elapsed_s);

    lv_color_t c = ((energy_wh  * QUARTER_S ) < (maxP * elapsed_s)) ? lv_color_hex(0x00ffff) : lv_color_hex(0xff0000);

    lv_obj_set_style_arc_color(arc_cur_peak, c, LV_PART_INDICATOR);

    lv_obj_set_style_text_color(lbl_cur_peak, c, 0);
    lv_obj_set_style_text_color(lbl_cur_peak_t, c, 0);
    lv_label_set_text_fmt(lbl_cur_peak, "%d", energy_wh );

    int left = QUARTER_S - elapsed_s;
    lv_label_set_text_fmt(lbl_15_min, "left: %d:%02d", left / 60, left % 60);
}

void my_ui_set_solar(int watts, int today, bool valid)
{
    if (arc_solar == NULL || lbl_solar == NULL) return;

    if (!valid) {
        lv_arc_set_value(arc_solar, 0);
        lv_label_set_text(lbl_solar, "--");
        lv_label_set_text(lbl_solar_today, "Today: -- kWh");
        return;
    }

    int v = watts;
    if (v < 0) v = 0;
    if (v > SOLAR_MAX_W) v = SOLAR_MAX_W;
    lv_arc_set_value(arc_solar, v);
    lv_label_set_text_fmt(lbl_solar, "%d", watts);
    lv_label_set_text_fmt(lbl_solar_today, "Today: %d.%02d kWh", today / 100, today % 100);
}

void my_ui_set_weather(double temperature,  int weather_code,double wind_speed,  bool valid)
{
    if (lbl_weather == NULL) return;

    if (!valid ) {
        lv_label_set_text(lbl_weather, "Oostende: --");
        return;
    }

    char text[160];
    snprintf(text, sizeof(text), "                  temp : %.0f - %s    Wind: %.0f km/h                      ", temperature,  weather_description(weather_code), wind_speed);
    lv_label_set_text(lbl_weather, text);
}

void my_ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x003366), 0);

    // definitions: 4 arcs, 5 labels for values, 5 labels for titles, 1 label for clock, 1 label for weather
    arc_grid_pow = lv_arc_create(scr);
    lv_obj_set_size(arc_grid_pow, ARC_SIZE_L, ARC_SIZE_L);
    lv_obj_center(arc_grid_pow);
    lv_obj_set_x(arc_grid_pow, -X_OFFSET);
    lv_obj_set_style_arc_width(arc_grid_pow, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_clickable(arc_grid_pow, false);
    lv_obj_remove_style(arc_grid_pow, NULL, LV_PART_KNOB);   /* no knob: display only */
    lv_arc_set_range(arc_grid_pow, -POWER_MAX_W, POWER_MAX_W);
    lv_arc_set_mode(arc_grid_pow, LV_ARC_MODE_SYMMETRICAL);
    lv_arc_set_value(arc_grid_pow, 0);
    lv_obj_set_style_arc_width(arc_grid_pow, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_grid_pow, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc_grid_pow, LV_OPA_COVER, LV_PART_MAIN);

    /* Arc: inverter output (0..SOLAR_MAX_W) */
    arc_solar = lv_arc_create(scr);
    lv_obj_set_size(arc_solar, ARC_SIZE_S, ARC_SIZE_S);
    lv_obj_center(arc_solar);
    lv_obj_set_x(arc_solar, -X_OFFSET);
    lv_obj_set_clickable(arc_solar, false);
    lv_obj_remove_style(arc_solar, NULL, LV_PART_KNOB);
    lv_arc_set_bg_angles(arc_solar, 2, 135);
    lv_arc_set_rotation(arc_solar, 270);             /* start at 12 o'clock */
    lv_arc_set_range(arc_solar, 0, SOLAR_MAX_W);
    lv_arc_set_value(arc_solar, 0);

    lv_obj_set_style_arc_width(arc_solar, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_solar, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc_solar, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc_solar, ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_solar, lv_color_hex(0xF0B000), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc_solar, LV_OPA_COVER, LV_PART_INDICATOR);
/*--------------------------------------------------------------------*/

    /* Arc: progress of the current quarter-hour */
    arc_cur_peak = lv_arc_create(scr);
    lv_obj_set_size(arc_cur_peak, ARC_SIZE_S, ARC_SIZE_S);
    lv_obj_center(arc_cur_peak);
    lv_obj_set_x(arc_cur_peak, X_OFFSET);
    lv_obj_set_clickable(arc_cur_peak, false);
    lv_obj_remove_style(arc_cur_peak, NULL, LV_PART_KNOB);
    lv_arc_set_bg_angles(arc_cur_peak, 135, 405);
    lv_arc_set_rotation(arc_cur_peak, 0);
    lv_arc_set_range(arc_cur_peak, 0, 2500 );   /* Wh in 15 min */
    lv_arc_set_value(arc_cur_peak, 0);

    /* Track (background ring): visible even at value 0 */
    lv_obj_set_style_arc_width(arc_cur_peak, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_cur_peak, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc_cur_peak, LV_OPA_COVER, LV_PART_MAIN);

    /* Indicator (the filled part) */
    lv_obj_set_style_arc_width(arc_cur_peak, ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_cur_peak, lv_color_hex(0x2090E0), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc_cur_peak, LV_OPA_COVER, LV_PART_INDICATOR);
/*---------------------------------------------------------------------------*/
    
    /* Arc Elapsed time of the running quarter-hour (0..900 s) */
    arc_15_min = lv_arc_create(scr);
    lv_obj_set_size(arc_15_min, ARC_SIZE_L, ARC_SIZE_L);
    lv_obj_center(arc_15_min);
    lv_obj_set_x(arc_15_min, X_OFFSET);
    lv_obj_set_clickable(arc_15_min, false);
    lv_obj_remove_style(arc_15_min, NULL, LV_PART_KNOB);
    lv_arc_set_bg_angles(arc_15_min, 135, 405);
    lv_arc_set_rotation(arc_15_min, 0);              /* start at 12 o'clock */
    lv_arc_set_range(arc_15_min, 0, QUARTER_S);
    lv_arc_set_value(arc_15_min, 0);
  
  // Background track (grey)
  lv_obj_set_style_arc_color(arc_15_min, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_15_min, ARC_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(arc_15_min, LV_OPA_COVER, LV_PART_MAIN);

  // Foreground indicator (blue-grey)
  lv_obj_set_style_arc_color(arc_15_min, lv_color_hex(CLOCK_CLR), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc_15_min, ARC_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc_15_min, true, LV_PART_INDICATOR);
  /*---------------------------------------------------------------------------*/
  
arc_used_pow = lv_arc_create(scr);
    lv_obj_set_size(arc_used_pow, ARC_SIZE_S, ARC_SIZE_S );
    lv_obj_center(arc_used_pow);
    lv_obj_set_x(arc_used_pow, -X_OFFSET);
    lv_obj_set_clickable(arc_used_pow, false);
    lv_obj_remove_style(arc_used_pow, NULL, LV_PART_KNOB);
    lv_arc_set_bg_angles(arc_used_pow, 135, 268);   /* bottom, around the left, to top */
    lv_arc_set_rotation(arc_used_pow, 0);
    lv_arc_set_mode(arc_used_pow, LV_ARC_MODE_REVERSE);
    lv_arc_set_range(arc_used_pow, 0, POWER_MAX_W);
    lv_arc_set_value(arc_used_pow, 0);
    
  
    // Background track (grey)
    lv_obj_set_style_arc_color(arc_used_pow, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_used_pow, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc_used_pow, LV_OPA_COVER, LV_PART_MAIN);

    // Foreground indicator (blue-grey)
    lv_obj_set_style_arc_color(arc_used_pow, lv_color_hex(CON_CLR), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_used_pow, ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_used_pow, true, LV_PART_INDICATOR);

/*-----------------------------------------------------------------*/

    lbl_clock = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_clock, lv_color_hex(CLOCK_CLR), 0);
    lv_obj_set_style_text_letter_space(lbl_clock, 2, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_BOTTOM_MID, 0, -24);
    my_ui_set_clock();

    lbl_weather = lv_label_create(scr);

    lv_obj_set_size(lbl_weather, 820, 150);                 // width small enough to trigger scrolling
    lv_label_set_long_mode(lbl_weather, LV_LABEL_LONG_SCROLL_CIRCULAR);  // or LV_LABEL_LONG_SCROLL
    lv_obj_set_style_text_font(lbl_weather, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(lbl_weather, lv_color_hex(0x80D8FF), 0);
    lv_obj_set_style_text_align(lbl_weather, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_weather, LV_ALIGN_TOP_MID, 0, 30);

    my_ui_set_weather(0.0, -1, 0.0, false);

    lbl_grid_power_t =lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_grid_power_t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_grid_power_t, lv_color_hex(PURPLE), 0);
    lv_obj_align(lbl_grid_power_t, LV_ALIGN_CENTER, -X_OFFSET , Y_OFFSET_1_L);
    lv_label_set_text(lbl_grid_power_t, "Grid");

    lbl_mon_peak_t = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_mon_peak_t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_mon_peak_t, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_align(lbl_mon_peak_t, LV_ALIGN_CENTER, X_OFFSET, Y_OFFSET_1_R);
    lv_label_set_text(lbl_mon_peak_t, "Monthly Peak");

    lbl_cur_peak_t = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_cur_peak_t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_cur_peak_t, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_align(lbl_cur_peak_t, LV_ALIGN_CENTER, X_OFFSET, Y_OFFSET_3_R);
    lv_label_set_text(lbl_cur_peak_t, "Current Peak");

    lbl_solar_t = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_solar_t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_solar_t, lv_color_hex(0xF0B000), 0);
    lv_obj_align(lbl_solar_t, LV_ALIGN_CENTER, -X_OFFSET + 100, Y_OFFSET_3_L);
    lv_label_set_text(lbl_solar_t, "Solar");

    lbl_used_pow_t = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_used_pow_t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_used_pow_t, lv_color_hex(CON_CLR), 0);
    lv_obj_align(lbl_used_pow_t, LV_ALIGN_CENTER, -X_OFFSET -100 , Y_OFFSET_3_L);
    lv_label_set_text(lbl_used_pow_t, "Home");
    /*--------------------------------------------------------*/

    lbl_solar = lv_label_create(scr);
    lv_label_set_text(lbl_solar, "");
    lv_obj_set_style_text_font(lbl_solar, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_solar, lv_color_hex(0xF0B000), 0);
    lv_obj_align(lbl_solar, LV_ALIGN_CENTER, -X_OFFSET + 100, Y_OFFSET_4_L);

    lbl_solar_today = lv_label_create(scr);
    lv_label_set_text(lbl_solar_today, "");
    lv_obj_set_style_text_font(lbl_solar_today, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_solar_today, lv_color_hex(0xF0B000), 0);
    lv_obj_align(lbl_solar_today, LV_ALIGN_CENTER, -X_OFFSET , Y_OFFSET_7_L);

    lbl_grid_power = lv_label_create(scr);
    lv_label_set_text(lbl_grid_power, "");
    lv_obj_set_style_text_font(lbl_grid_power, &lv_font_montserrat_48, 0);
    //lv_obj_set_style_text_color(lbl_grid_power, lv_color_hex(0xFF0000), 0);
    lv_obj_align(lbl_grid_power, LV_ALIGN_CENTER, -X_OFFSET , Y_OFFSET_2_L);

    lbl_cur_peak = lv_label_create(scr);
    lv_label_set_text(lbl_cur_peak, "---");
    lv_obj_set_style_text_font(lbl_cur_peak, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_cur_peak, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_align(lbl_cur_peak, LV_ALIGN_CENTER, X_OFFSET, Y_OFFSET_4_R);

    lbl_mon_peak = lv_label_create(scr);
    lv_label_set_text(lbl_mon_peak, "");
    lv_obj_set_style_text_font(lbl_mon_peak, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_mon_peak, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_set_style_text_align(lbl_mon_peak, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_mon_peak, LV_ALIGN_CENTER, X_OFFSET, Y_OFFSET_2_R);  

    lbl_15_min = lv_label_create(scr);
    lv_label_set_text(lbl_15_min, "");
    lv_obj_set_style_text_font(lbl_15_min, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_15_min, lv_color_hex(CLOCK_CLR), 0);
    lv_obj_set_style_text_align(lbl_15_min, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_15_min, LV_ALIGN_CENTER, X_OFFSET, Y_OFFSET_5_R);  

    lbl_used_pow = lv_label_create(scr);
    lv_label_set_text(lbl_used_pow, "");
    lv_obj_set_style_text_font(lbl_used_pow, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_used_pow, lv_color_hex(CON_CLR), 0);
    lv_obj_set_style_text_align(lbl_used_pow, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_used_pow, LV_ALIGN_CENTER, -X_OFFSET  - 100, Y_OFFSET_4_L);  
    }

    // UNIVERSAL ANIMATION FUNCTION – works for ANY arc
static lv_anim_t arc_anim;

void animate_arc_to_zero(lv_obj_t *arc, int delay_ms) {
  if (!arc) return;

  int16_t current = lv_arc_get_value(arc);
  if (current <= 0) {
    lv_arc_set_value(arc, 0);
    return;
  }

  // Cancel any previous animation on this exact arc + callback combination
  lv_anim_del(arc, (lv_anim_exec_xcb_t)lv_arc_set_value);

  lv_anim_init(&arc_anim);
  lv_anim_set_var(&arc_anim, arc);
  lv_anim_set_values(&arc_anim, current, 0);
  lv_anim_set_time(&arc_anim, delay_ms);  // 
  lv_anim_set_exec_cb(&arc_anim, (lv_anim_exec_xcb_t)lv_arc_set_value);
  lv_anim_set_path_cb(&arc_anim, lv_anim_path_ease_out);  // nicer than linear
  lv_anim_start(&arc_anim);
}
