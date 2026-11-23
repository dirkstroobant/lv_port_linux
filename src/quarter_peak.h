#ifndef QUARTER_PEAK_H
#define QUARTER_PEAK_H

#include <cstdio>
#include <ctime>
#include <mutex>

// Energy used in the running quarter-hour = import counter now
// minus the import counter at the start of the quarter (:00/:15/:30/:45).
class QuarterPeak {
public:
    struct Values {
        int current_avg_w;
        int peak_w;
        int seconds_in_quarter;
        int energy_wh;
        bool complete_quarter;
    };

    QuarterPeak() : _started_slot(time(NULL) / QUARTER_S) { load(); }

    void addSample(long total_wh) {
        long now = time(NULL);
        long slot = now / QUARTER_S;

        std::lock_guard<std::mutex> lock(_m);

        if (slot != _slot || total_wh < _start_wh) {
            // new quarter (or counter went backwards): take the snapshot
           /* if (slot == _slot + 1)
                _last_wh_total = (_now_wh - _start_wh);   // finished quarter
                */
            _slot = slot;
            _start_wh = total_wh;
            save();
        }
        _now_wh = total_wh;
    }

    Values get() const {
        long now = time(NULL);
        long slot = now / QUARTER_S;

        std::lock_guard<std::mutex> lock(_m);
        Values v{};
        //v.last_avg_w = (int)(_last_wh_total * 4);
        v.complete_quarter = (slot > _started_slot)
                           && _slot == slot
                           && _now_wh >= _start_wh;
        if (v.complete_quarter) {
            v.seconds_in_quarter = (int)(now - slot * QUARTER_S);
            v.energy_wh = (int)(_now_wh - _start_wh);
            if (v.seconds_in_quarter > 0)
                v.current_avg_w = (int)(v.energy_wh * 3600L / v.seconds_in_quarter);
        }
        return v;
    }

    long startWh() const { std::lock_guard<std::mutex> l(_m); return _start_wh; }
    long nowWh()   const { std::lock_guard<std::mutex> l(_m); return _now_wh; }

private:
    static constexpr long QUARTER_S = 900;
    static constexpr const char* STATE_FILE = "/var/tmp/homewiz_quarter.txt";

    void save() const {
        FILE* f = fopen(STATE_FILE, "w");
        if (!f) return;
        fprintf(f, "%ld %ld\n", _slot, _start_wh);
        fclose(f);
    }

    void load() {
        FILE* f = fopen(STATE_FILE, "r");
        if (!f) return;
        long s, w;
        if (fscanf(f, "%ld %ld", &s, &w) == 2) { _slot = s; _start_wh = w; _now_wh = w; }
        fclose(f);
    }

    mutable std::mutex _m;
    long _started_slot;
    long _slot = -1;
    long _start_wh = 0;       // counter at the start of the quarter
    long _now_wh = 0;         // latest counter
    //long _last_wh_total = 0;  // energy of the last finished quarter
};

#endif