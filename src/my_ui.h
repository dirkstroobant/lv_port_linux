#ifndef MY_UI_H
#define MY_UI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void my_ui_create(void);
void my_ui_set_clock(void);
void my_ui_set_grid_pow(int watts, bool valid);
void my_ui_set_used_pow(int watts, bool valid);
void my_ui_set_mon_peak(int watts,bool valid);
void my_ui_set_cur_peak(int energy_wh, int max_demand_w, int elapsed_s, bool valid);
void my_ui_set_solar(int watts, int today, bool valid);
void my_ui_set_weather(double temperature,  int weather_code, double wind_speed,  bool valid);



#ifdef __cplusplus
}
#endif

#endif