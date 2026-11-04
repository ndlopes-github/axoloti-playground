/*
 * axo_dfu.c: reboot an Axoloti Core into the STM32 ROM DFU bootloader (Rescue
 * Mode) without pressing S1, by sending the patcher's "go to DFU" command.
 *
 * Protocol (read from Ksoloti's sources, tags 1.0.12-8 and 1.1.0):
 *   - commands are 4 bytes, "Axo" + a letter, written to bulk OUT endpoint
 *     0x02 of interface 2 (USBBulkConnection.java);
 *   - 1.0.12-era firmware: "AxoD" -> StopPatch(); exception_initiate_dfu()
 *     (pconnection.c); 1.1.0 firmware renamed it "Axou" (use -n);
 *   - exception_initiate_dfu() stores a magic in backup SRAM and soft-resets;
 *     at boot exception_check_DFU() clears the magic and only then jumps to
 *     the ROM bootloader, so the next power cycle boots the firmware again:
 *     the request cannot latch.
 * Firmware that does not know the letter ignores it (its parser resets).
 *
 * The firmware only reads input while no acknowledgement is pending
 * (PExReceive: if (!AckPending)), and only sends that acknowledgement once its
 * output queue is empty (PExTransmit). So a host that never reads can leave
 * the command unread: we drain IN endpoint 0x82 before and after writing.
 * One case draining cannot fix: a parser left halfway through a multi-byte
 * command by an earlier host, which takes our bytes as that command's
 * payload. Replugging the USB cable resets it; run this right after.
 *
 * The board then enumerates as 0483:df11; flash it with the patcher's
 * Board > Firmware > Flash (Rescue). Close the patcher first, or its
 * connection attempts may hold interface 2 (claim fails with BUSY).
 *
 * Declares the few libusb-1.0 calls it uses, so it builds against the runtime
 * library alone (no libusb-1.0-0-dev):
 *   cc -O2 -Wall -o axo_dfu axo_dfu.c -l:libusb-1.0.so.0
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct libusb_context libusb_context;
typedef struct libusb_device_handle libusb_device_handle;
int libusb_init(libusb_context **ctx);
void libusb_exit(libusb_context *ctx);
libusb_device_handle *libusb_open_device_with_vid_pid(libusb_context *ctx, uint16_t vid, uint16_t pid);
void libusb_close(libusb_device_handle *h);
int libusb_claim_interface(libusb_device_handle *h, int iface);
int libusb_release_interface(libusb_device_handle *h, int iface);
int libusb_bulk_transfer(libusb_device_handle *h, unsigned char ep, unsigned char *data, int len, int *transferred,
                         unsigned int timeout_ms);
const char *libusb_error_name(int code);

enum { VID_AXOLOTI = 0x16c0, PID_AXOLOTI = 0x0442, IFACE = 2, EP_OUT = 0x02, EP_IN = 0x82, TIMEOUT_MS = 1000 };
enum { LIBUSB_ERROR_TIMEOUT = -7 };

/* Reads IN until it has been quiet for quiet_ms, at most rounds times; prints
 * a printable rendering of what came (acks "AxoA", text "AxoT", ...).
 * Returns the libusb code that ended it: TIMEOUT is the normal end; any other
 * error after the DFU command usually means the board has reset. */
static int drain(libusb_device_handle *h, unsigned quiet_ms, int rounds, const char *label)
{
    unsigned char buf[4096];
    long total = 0;
    int r = 0;
    for (int i = 0; i < rounds; i++) {
        int got = 0;
        r = libusb_bulk_transfer(h, EP_IN, buf, (int)sizeof buf, &got, quiet_ms);
        if (got > 0) {
            if (total == 0)
                printf("%s:", label);
            putchar(' ');
            for (int k = 0; k < got && k < 48; k++)
                putchar(buf[k] >= 32 && buf[k] < 127 ? buf[k] : '.');
            total += got;
        }
        if (r < 0)
            break;
    }
    if (total > 0)
        printf("  [%ld bytes]\n", total);
    return r;
}

static int send4(libusb_device_handle *h, const char *cmd)
{
    unsigned char buf[4];
    memcpy(buf, cmd, sizeof buf);
    int sent = 0;
    int r = libusb_bulk_transfer(h, EP_OUT, buf, (int)sizeof buf, &sent, TIMEOUT_MS);
    if (r < 0 || sent != (int)sizeof buf) {
        fprintf(stderr, "bulk write %s: %s (%d of %zu bytes)\n", cmd, libusb_error_name(r), sent, sizeof buf);
        return -1;
    }
    return 0;
}

/* "AxoV" -> reply "AxoV" + 4 version bytes + firmware id (ReplyFWVersion in
 * 1.0.12 pconnection.c). The reply may come mixed with acks and text, so
 * scan for the tag. Returns 0 when the firmware answered. */
static int probe_version(libusb_device_handle *h)
{
    if (send4(h, "AxoV") < 0)
        return -1;
    unsigned char buf[4096];
    int have = 0;
    for (int i = 0; i < 10 && have < (int)sizeof buf; i++) {
        int got = 0;
        int r = libusb_bulk_transfer(h, EP_IN, buf + have, (int)sizeof buf - have, &got, 200);
        have += got;
        if (r < 0 && r != LIBUSB_ERROR_TIMEOUT)
            break;
    }
    for (int k = 0; k + 12 <= have; k++)
        if (memcmp(buf + k, "AxoV", 4) == 0) {
            const unsigned char *v = buf + k + 4;
            printf("firmware answered: version %u.%u.%u.%u, id %02X%02X%02X%02X\n", v[0], v[1], v[2], v[3], v[4],
                   v[5], v[6], v[7]);
            return 0;
        }
    printf("no version reply (%d bytes came back): the firmware is not parsing our commands\n", have);
    return -1;
}

int main(int argc, char **argv)
{
    bool probe_only = argc > 1 && strcmp(argv[1], "-v") == 0;
    const char *cmd = (argc > 1 && strcmp(argv[1], "-n") == 0) ? "Axou" : "AxoD";
    libusb_context *ctx = NULL;
    int r = libusb_init(&ctx);
    if (r < 0) {
        fprintf(stderr, "libusb_init: %s\n", libusb_error_name(r));
        return 1;
    }
    libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx, VID_AXOLOTI, PID_AXOLOTI);
    if (!h) {
        fprintf(stderr, "no accessible Axoloti Core (16c0:0442): not plugged in, or udev rule missing\n");
        libusb_exit(ctx);
        return 1;
    }
    r = libusb_claim_interface(h, IFACE);
    if (r < 0) {
        fprintf(stderr, "claim interface %d: %s (close the patcher and retry)\n", IFACE, libusb_error_name(r));
        libusb_close(h);
        libusb_exit(ctx);
        return 1;
    }
    drain(h, 200, 50, "pending output");
    if (cmd[3] == 'D')
        probe_version(h); /* informative only: 1.1.0 firmware does not know "AxoV" */
    if (probe_only || send4(h, cmd) < 0) {
        libusb_release_interface(h, IFACE);
        libusb_close(h);
        libusb_exit(ctx);
        return probe_only ? 0 : 1;
    }
    printf("sent %s\n", cmd);
    r = drain(h, 300, 10, "reply");
    if (r != LIBUSB_ERROR_TIMEOUT)
        printf("connection ended (%s): the board is resetting\n", libusb_error_name(r));
    printf("the board should reappear as 0483:df11 within a few seconds\n");
    libusb_release_interface(h, IFACE); /* may fail once the board has reset; harmless */
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
