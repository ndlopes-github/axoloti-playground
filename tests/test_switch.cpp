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
static bool armed;
static int cc16(uint8_t n, uint8_t v = 127) { return ps.midi(0xBF, n, v, &armed); }
static int pad(uint8_t note, uint8_t v = 100) { return ps.midi(0x99, note, v, &armed); }
static void wait_ms(int ms) { for (int i = 0; i < ms * 3; i++) ps.tick(); }

int main() {
    // together (50 ms apart), then pad 2 (note 41): slot 1, grains
    ps.init();
    wait_ms(1000);
    cc16(51);
    wait_ms(50);
    cc16(52);
    CHECK(armed, "not armed by a 50 ms pair");
    wait_ms(500);
    CHECK(pad(41) == 1, "pad 2 should load slot 1");
    CHECK(pad(41) == -1, "one load per arming");

    // in the other order, and with the buttons left of the pads (106, 107)
    cc16(107);
    wait_ms(100);
    cc16(106);
    CHECK(armed, "106/107 in either order");
    CHECK(pad(48) == 4, "pad 5 (note 48) = slot 4, aeolian");

    // two ordinary page changes, 300 ms apart: no arming, pads stay presets
    cc16(51);
    wait_ms(300);
    cc16(52);
    CHECK(!armed, "armed by page changes 300 ms apart");
    CHECK(pad(40) == -1, "a preset pad loaded a patch");

    // armed, but the pad comes after 3 s
    cc16(51); cc16(52);
    wait_ms(3100);
    CHECK(pad(40) == -1, "loaded after the 3 s window");

    // releases (value 0) do not count as presses; bottom pads and empty
    // slots (pads 6-8) do nothing
    cc16(51, 0); cc16(52, 0);
    CHECK(!armed, "armed by releases");
    cc16(51); cc16(52);
    CHECK(pad(36) == -1, "bottom pad loaded a patch");
    CHECK(pad(49) == -1 && pad(40) == -1, "pad 6 is an empty slot, and it disarms");
    cc16(51); cc16(52);
    CHECK(pad(40, 0) == -1 && pad(40) == 0, "a pad release does not load; the press does (slot 0, freak)");

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("patch switch: all tests passed\n");
    return 0;
}
