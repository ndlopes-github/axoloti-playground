// Host tests for the patch switcher's gesture (lib/patch_switch.h). make test
#include <cstdio>

#include "../lib/patch_switch.h"

using namespace freak;

static int failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            failures++;                                     \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                       \
            std::printf("\n");                              \
        }                                                   \
    } while (0)

static PatchSwitch ps;
static int cc16(uint8_t n, uint8_t v) { return ps.midi(0xBF, n, v); }
static int pad(uint8_t note, uint8_t v = 100) { return ps.midi(0x99, note, v); }
static void wait_ms(int ms) { for (int i = 0; i < ms * 3; i++) ps.tick(); }

int main() {
    ps.init();
    wait_ms(1000);
    // hold the button, tap pad 2 (note 41): slot 1, grains
    cc16(107, 127);
    wait_ms(300);
    CHECK(pad(41) == 1, "held + pad 2 should load slot 1");
    CHECK(pad(41) == -1, "one load per hold");
    cc16(107, 0);

    // pad 6 (note 49) = slot 5, raw; pad 5 (note 48) = slot 4, aeolian
    cc16(107, 127);
    CHECK(pad(49) == 5, "pad 6 = raw");
    cc16(107, 0);
    cc16(107, 127);
    CHECK(pad(48) == 4, "pad 5 = aeolian");
    cc16(107, 0);

    // a page change (press and release), then a preset pad: no load
    cc16(107, 127);
    wait_ms(80);
    cc16(107, 0);
    wait_ms(200);
    CHECK(pad(40) == -1, "a preset pad after a page change loaded a patch");

    // held for more than 3 s: no load (a lost release cannot arm for long)
    cc16(107, 127);
    wait_ms(3100);
    CHECK(pad(40) == -1, "loaded after 3 s of holding");
    cc16(107, 0);

    // while held: bottom pads, pad releases and the empty slot (pad 8)
    // do nothing, and the hold stays for a top pad; pad 7 is quartet
    cc16(107, 127);
    CHECK(pad(36) == -1, "bottom pad loaded a patch");
    CHECK(pad(40, 0) == -1, "a pad release loaded a patch");
    CHECK(pad(51) == -1, "pad 8 is an empty slot");
    CHECK(pad(50) == 6, "pad 7 = quartet");
    cc16(107, 127);
    CHECK(pad(40) == 0, "then pad 1 = freak");
    cc16(107, 0);

    // the other buttons, and the old gesture, do nothing
    cc16(51, 127); cc16(52, 127); cc16(106, 127);
    CHECK(pad(40) == -1, "Device Select + Device Lock no longer arm");
    cc16(51, 0); cc16(52, 0); cc16(106, 0);
    // the button on another channel does not arm
    ps.midi(0xB0, 107, 127);
    CHECK(pad(40) == -1, "CC 107 on channel 1 armed");

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("patch switch: all tests passed\n");
    return 0;
}
