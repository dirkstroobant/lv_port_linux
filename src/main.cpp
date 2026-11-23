#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctime>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "lvgl/lvgl.h"
#include "my_ui.h"
#include "p1_meter.h"
#include "solar_inverter.h"
#include "quarter_peak.h"
#include "weather.h"

#define DRM_DEVICE   "/dev/dri/card0"
#define INPUT_DEVICE "/dev/input/event0"

/* Adjust to your network */
#define INVERTER_HOST "192.168.1.34"
#define INVERTER_USER "admin"
#define INVERTER_PASS "admin"
#define P1_HOST       ""   /* "" = look it up with avahi-browse */

#define POLL_P1_S       2      /* P1 meter: quarter energy needs frequent reads */
#define POLL_INV_S      4      /* inverter: slower is fine */
#define POLL_WEATHER_S  300 /* Open-Meteo: weather data changes slowly */

static volatile sig_atomic_t running = 1;


static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static uint32_t tick_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Data sources, shared between the worker threads and the LVGL timer */
struct Sources {
    SolarInverter *inv;
    P1Meter *meter;
    QuarterPeak *qp;
    WeatherData *weather;
};

/* Generic polling loop: runs fn() every `seconds` (start to start).
   Sleeps in 50 ms steps so the thread stops quickly on shutdown. */
template <typename F>
static void poll_loop(std::atomic<bool> *run, int seconds, F fn)
{
    using clock = std::chrono::steady_clock;
    while (*run) {
        auto start = clock::now();
        fn();
        while (*run && clock::now() - start < std::chrono::seconds(seconds))
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

/* LVGL timer (runs in the LVGL thread): only reads stored values, updates UI */
static void ui_refresh_cb(lv_timer_t *t)
{
    Sources *s = static_cast<Sources *>(lv_timer_get_user_data(t));
    my_ui_set_clock();
    meter_data d = s->meter->getData();

    /* Net grid power: positive = importing, negative = exporting */
    int net_w = (int)(d.active_power_W - d.return_power_W);
    bool meter_valid = s->meter->lastReadOk();
    my_ui_set_grid_pow(net_w, meter_valid);

    QuarterPeak::Values q = s->qp->get();
    int elapsed = (int)(time(NULL) % 900);
    my_ui_set_cur_peak(q.energy_wh, (int)d.max_demand_W, elapsed, q.complete_quarter);

    my_ui_set_mon_peak((int)d.max_demand_W, s->meter->lastReadOk());

    int solar_w = s->inv->getOutputPower();
    bool solar_valid = s->inv->lastReadOk();
    my_ui_set_solar(solar_w, s->inv->getTodayEnergy(), solar_valid);
    
    int used_pow;
    if (solar_valid) {
        used_pow = net_w + solar_w;
    } else {
        used_pow = net_w;
    }

    my_ui_set_used_pow(used_pow, meter_valid);

    WeatherReading reading;
    bool weather_valid;
    {
        std::lock_guard<std::mutex> lock(s->weather->mutex);
        reading = s->weather->reading;
        weather_valid = s->weather->valid;
    }
    my_ui_set_weather(reading.temperature, reading.weather_code, reading.wind_speed, weather_valid);
}

int main(int argc, char **argv)
{
    const char *drm_path = (argc > 1) ? argv[1] : DRM_DEVICE;
    //const char *input_path = (argc > 2) ? argv[2] : INPUT_DEVICE;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    CURLcode curl_init_result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (curl_init_result != CURLE_OK) {
        fprintf(stderr, "Failed to initialize cURL: %s\n",
                curl_easy_strerror(curl_init_result));
    }

    lv_init();
    lv_tick_set_cb(tick_cb);

    /* Data sources: they live on the stack for the whole of main() */
    SolarInverter inverter(INVERTER_HOST);
    inverter.setCredentials(INVERTER_USER, INVERTER_PASS);
    P1Meter meter(P1_HOST);
    QuarterPeak qpeak;

    WeatherData weather;
    Sources sources{&inverter, &meter, &qpeak, &weather};

    /* Display: DRM/KMS, -1 = pick the first connected output */
    lv_display_t *disp = lv_linux_drm_create();
    if (disp == NULL) {
        fprintf(stderr, "Failed to create DRM display\n");
        if (curl_init_result == CURLE_OK) curl_global_cleanup();
        return 1;
    }

    lv_linux_drm_set_file(disp, "/dev/dri/card0", -1);

    my_ui_create();
    lv_timer_create(ui_refresh_cb, 1000, &sources);

    /* Start polling: one thread per device, so a slow or offline
       inverter can never delay the meter readings */
    std::atomic<bool> poll_run{true};

    std::thread t_inv([&] {
        poll_loop(&poll_run, POLL_INV_S, [&] {
            inverter.update();
            /*
            printf("inv ok=%d %d W today=%d\n",
                   (int)inverter.lastReadOk(), inverter.getOutputPower(),inverter.getTodayEnergy());
            fflush(stdout);
            */
        });
    });

    std::thread t_p1([&] {
        poll_loop(&poll_run, POLL_P1_S, [&] {
            meter.update();
            meter_data d = meter.getData();

            /* Require both counters: a partly parsed telegram would make the
               total drop and restart the quarter */
            if (meter.lastReadOk() && d.energy_T1 > 0 && d.energy_T2 > 0)
                qpeak.addSample(d.energy_T1 + d.energy_T2);
/*
            printf("p1 ok=%d import=%ld export=%ld max=%ld | "
                   "quarter start=%ld now=%ld used=%ld Wh\n",
                   (int)meter.lastReadOk(), d.active_power_W, d.return_power_W, d.max_demand_W,
                   qpeak.startWh(), qpeak.nowWh(), qpeak.nowWh() - qpeak.startWh());
            fflush(stdout);
            */
        });
    });

    std::thread t_weather;
    if (curl_init_result == CURLE_OK) {
        t_weather = std::thread([&] {
            poll_loop(&poll_run, POLL_WEATHER_S, [&] {
                WeatherReading reading;
                bool valid = fetch_weather(&reading);

                std::lock_guard<std::mutex> lock(weather.mutex);
                weather.valid = valid;
                if (valid) weather.reading = std::move(reading);
            });
        });
    }

    while (running) {
        uint32_t delay = lv_timer_handler();
        if (delay < 1) delay = 1;
        if (delay > 20) delay = 20;
        usleep(delay * 1000);
    }

    /* Stop the threads before the data sources go out of scope */
    poll_run = false;
    t_inv.join();
    t_p1.join();
    if (t_weather.joinable()) t_weather.join();

    lv_deinit();
    if (curl_init_result == CURLE_OK) curl_global_cleanup();
    return 0;
}