// patch_switch.h: switch between the patches on the SD card from the
// Launchkey, without touching the firmware. Every patch on the card carries
// one objects/patch_switch.axo.
//
// Gesture: press Device Select and Device Lock together (within kWindow of
// each other, in either order; the buttons left of the pads, CC 106 and
// 107, count as the same pair), then, within kArmed, a top pad: pad n
// loads slot n. In every patch those two buttons only switch knob pages, so
// the gesture has no lasting side effect; the window keeps two ordinary
// page changes in a row from arming it. The armed state does not depend on
// release messages.
//
// Loading (objects/patch_switch.axo, on the board): the firmware's
// LoadPatch("/dir/patch.bin"), the same call as the factory patch/load
// object. It stops the running patch and loads the file in the DSP thread.
// A load by file name that fails leaves the board silent (no fallback, unlike
// a load by index), so the object first checks that the file exists with
// f_stat; a missing patch is logged and ignored. "Upload to SD Card" in the
// patcher writes a patch to /<name>/patch.bin, with the files it depends on.
//
// Threads: the MIDI handlers run in threads with 256-byte stacks (USB device
// MIDI, serial MIDI), and FatFs puts a 512-byte long-file-name buffer on the
// stack (_USE_LFN 2): f_stat in a MIDI handler hung the board. So the
// handler only runs PatchSwitch::midi (arithmetic) and posts the slot; the
// object's k-rate code (the DSP thread, 7.3 KB of stack) does the f_stat,
// the log and LoadPatch.
//
// Header-only, C++11, a POD; the logic is tested on the host.
#ifndef PATCH_SWITCH_H
#define PATCH_SWITCH_H

#include <stdint.h>

namespace freak {

// Slots, top pads left to right.
static const char *const kSwitchSlots[] = {
    "/freak_v1/patch.bin", "/grains_v1/patch.bin", "/groove_v1/patch.bin",
    "/soliton_v1/patch.bin", "/aeolian_v1/patch.bin",
};
const int kNumSwitchSlots = sizeof(kSwitchSlots) / sizeof(kSwitchSlots[0]);

struct PatchSwitch {
    static const uint32_t kWindow = 360;   // 120 ms of control ticks (3 kHz)
    static const uint32_t kArmed = 9000;   // 3 s
    uint32_t t;                 // control ticks (written by the DSP thread only)
    uint32_t t_sel, t_lock;     // last press of each button, 0 = never
    uint32_t armed_until;       // 0 = not armed

    void init() { t = 1; t_sel = t_lock = 0; armed_until = 0; }
    void tick() { t++; }

    // Raw MIDI. Returns the slot to load, or -1. Sets *armed when this
    // message armed the gesture (for the log).
    int midi(uint8_t status, uint8_t d1, uint8_t d2, bool *armed) {
        *armed = false;
        const uint8_t type = status & 0xF0, ch = status & 0x0F;
        const uint32_t now = t;
        if (type == 0xB0 && ch == 15 && d2 > 0) {
            if (d1 == 51 || d1 == 106) t_sel = now;
            else if (d1 == 52 || d1 == 107) t_lock = now;
            else return -1;
            if (t_sel && t_lock) {
                const uint32_t d = t_sel > t_lock ? t_sel - t_lock : t_lock - t_sel;
                if (d <= kWindow) {
                    armed_until = now + kArmed;
                    t_sel = t_lock = 0;  // one arming per pair of presses
                    *armed = true;
                }
            }
            return -1;
        }
        if (type == 0x90 && ch == 9 && d2 > 0 && armed_until && now <= armed_until) {
            // top row: notes 40-43, 48-51 = pads 1-8
            int pad = -1;
            if (d1 >= 40 && d1 <= 43) pad = d1 - 40;
            else if (d1 >= 48 && d1 <= 51) pad = d1 - 44;
            if (pad < 0) return -1;
            armed_until = 0;
            return pad < kNumSwitchSlots ? pad : -1;
        }
        return -1;
    }
};

}  // namespace freak

#endif
