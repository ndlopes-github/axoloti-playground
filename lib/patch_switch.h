// patch_switch.h: switch between the patches on the SD card from the
// Launchkey, without touching the firmware. Every patch on the card carries
// one objects/patch_switch.axo.
//
// Gesture: hold both buttons left of the pads and tap a top pad: pad n
// loads slot n (works on the Launchkey, Nuno, 2026-11-13): 1 freak, 2 grains,
// 3 groove, 4 soliton, 5 aeolian, 6 raw, 7 quartet. The code arms
// on CC 107 held; holding both buttons makes sure it is. Holding one of
// them alone did not switch for Nuno, so the button that sends 107 may be
// the upper one, not the lower one as docs/launchkey49mk3_map.md has it
// (conjecture, not logged). In every patch those buttons only switch the
// knob page, so the gesture changes no sound. The switch is armed only
// while CC 107 is held: from its press (value > 0) to its release (value
// 0), and at most kHoldMax after the press. So a page change followed by a
// preset pad never loads a patch, if the button sends its release (assumed,
// not logged; the cap bounds the armed time either way). (The earlier
// gesture, Device Select + Device Lock, then a pad, is gone: Nuno asked for
// another.)
// Shift + a key was the first idea, but Shift sends no MIDI in standalone
// mode, and while it is held the keys send nothing either (observed
// 2026-11-13).
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
    "/soliton_v1/patch.bin", "/aeolian_v1/patch.bin", "/raw_v1/patch.bin",
    "/quartet_v1/patch.bin",
};
const int kNumSwitchSlots = sizeof(kSwitchSlots) / sizeof(kSwitchSlots[0]);
const uint8_t kSwitchButton = 107;  // a button left of the pads (ch 16); see above

struct PatchSwitch {
    static const uint32_t kHoldMax = 9000;  // 3 s of control ticks (3 kHz)
    uint32_t t;            // control ticks (written by the DSP thread only)
    uint32_t held_since;   // press of the button, 0 = not held

    void init() { t = 1; held_since = 0; }
    void tick() { t++; }

    // Raw MIDI. Returns the slot to load, or -1.
    int midi(uint8_t status, uint8_t d1, uint8_t d2) {
        const uint8_t type = status & 0xF0, ch = status & 0x0F;
        if (type == 0xB0 && ch == 15 && d1 == kSwitchButton) {
            held_since = d2 > 0 ? t : 0;
            return -1;
        }
        if (type == 0x90 && ch == 9 && d2 > 0 && held_since && t - held_since <= kHoldMax) {
            // top row: notes 40-43, 48-51 = pads 1-8
            int pad = -1;
            if (d1 >= 40 && d1 <= 43) pad = d1 - 40;
            else if (d1 >= 48 && d1 <= 51) pad = d1 - 44;
            if (pad < 0 || pad >= kNumSwitchSlots) return -1;
            held_since = 0;  // one load per hold
            return pad;
        }
        return -1;
    }
};

}  // namespace freak

#endif
