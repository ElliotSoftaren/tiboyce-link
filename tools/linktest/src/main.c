/*
 * LINKTEST - calculator-to-calculator USB link test for tiboyce-link
 *
 * Connect two TI-84 Plus CE calculators with a USB mini-A <-> mini-B cable
 * and run LINKTEST on both. The calculator with the mini-A plug becomes the
 * USB host and runs the tests; the other one (device) answers.
 *
 * Tests (run on the host when ENTER is pressed):
 *   1. Ping:     500 single-byte round trips, measures latency (min/avg/max)
 *   2. Exchange: 1000 Game Boy style byte swaps (both sides send one byte and
 *                receive the other's byte); both sides verify every byte
 *
 * Packets are always 2 bytes: [command, value].
 */

#include "linklib.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <graphx.h>
#include <ti/getcsc.h>
#include <ti/screen.h>

#define VERSION "v0.1"

#define PING_COUNT      500
#define EXCHANGE_COUNT  1000
#define TIMEOUT_TICKS   (CLOCKS_PER_SEC)       /* 1 second per reply */

#define CMD_RESET     'R'
#define CMD_PING      'P'
#define CMD_EXCHANGE  'X'
#define CMD_DONE      'D'
#define REPLY(cmd)    ((char)((cmd) | 0x20))   /* lowercase = reply */

/* ------------------------------------------------------------------ */
/* Screen output                                                       */
/* ------------------------------------------------------------------ */

#define LINE_HEIGHT 10
#define MAX_LINES   (GFX_LCD_HEIGHT / LINE_HEIGHT)

static int line;

static void cls(void) {
    gfx_FillScreen(255);
    line = 0;
}

static void println(const char *s) {
    if (line >= MAX_LINES)
        cls();
    gfx_PrintStringXY(s, 2, 2 + line * LINE_HEIGHT);
    line++;
}

static char txt[64];
#define printf_line(...) do { snprintf(txt, sizeof(txt), __VA_ARGS__); println(txt); } while (0)

/* ------------------------------------------------------------------ */
/* Link helpers                                                        */
/* ------------------------------------------------------------------ */

/* Static buffers: a timed-out transfer may still complete later, so the
 * memory it points at must stay valid. */
static uint8_t rx_buf[64];
static size_t rx_len;          /* LINK_PENDING while a read is queued */
static bool rx_queued;

static uint8_t tx_buf[2];
static size_t tx_len;
static bool tx_queued;

static uint8_t peer;           /* USB address of the other calculator */
static bool user_quit;

static bool connected(void) {
    const link_devices_t *d = link_Devices();
    if (!d->count)
        return false;
    peer = d->ids[0];
    return true;
}

static void reset_link_state(void) {
    rx_queued = tx_queued = false;
}

/* Check the keyboard without slowing down tight loops too much. */
static bool check_quit(void) {
    uint8_t key = os_GetCSC();
    if (key == sk_Clear || key == sk_Mode)
        user_quit = true;
    return user_quit;
}

/* Send a 2-byte packet. Waits for the previous write to finish first. */
static bool send_packet(uint8_t cmd, uint8_t val) {
    clock_t start = clock();
    unsigned n = 0;

    while (tx_queued) {
        link_Poll();
        if (tx_len != LINK_PENDING)
            tx_queued = false;
        else if (clock() - start > TIMEOUT_TICKS)
            return false;
        if (!(++n & 0xFF) && check_quit())
            return false;
    }

    tx_buf[0] = cmd;
    tx_buf[1] = val;
    tx_len = sizeof(tx_buf);
    if (!link_QueueWrite(peer, tx_buf, &tx_len))
        return false;
    tx_queued = true;
    return true;
}

/* Receive a 2-byte packet. Returns false on timeout / quit / error.
 * timeout == 0 means wait forever (until quit or disconnect). */
static bool recv_packet(uint8_t *cmd, uint8_t *val, clock_t timeout) {
    clock_t start = clock();
    unsigned n = 0;

    for (;;) {
        if (!rx_queued) {
            rx_len = sizeof(rx_buf);
            if (!link_QueueRead(peer, rx_buf, &rx_len))
                return false;
            rx_queued = true;
        }

        link_Poll();

        /* keep the write side moving too */
        if (tx_queued && tx_len != LINK_PENDING)
            tx_queued = false;

        if (rx_len != LINK_PENDING) {
            rx_queued = false;
            if (rx_len >= 2) {
                *cmd = rx_buf[0];
                *val = rx_buf[1];
                return true;
            }
            /* zero-length or failed transfer: a new read is queued on the
             * next iteration; still check timeout/quit below */
        }

        if (timeout && clock() - start > timeout)
            return false;
        if (!(++n & 0xFF)) {
            if (check_quit())
                return false;
            if (!(n & 0xFFF) && !connected())
                return false;
        }
    }
}

/* Deterministic byte pattern so both sides can verify the data. */
static uint8_t host_byte(unsigned i)   { return (uint8_t)(i * 7 + 1); }
static uint8_t device_byte(unsigned i) { return (uint8_t)(i * 13 + 5) ^ 0xA5; }

static unsigned long ticks_to_us(unsigned long t) {
    /* 64-bit math: t * 1000000 overflows 32 bits after ~4300 ticks */
    return (unsigned long)((unsigned long long)t * 1000000ULL / CLOCKS_PER_SEC);
}

/* ------------------------------------------------------------------ */
/* Host side: runs the tests                                           */
/* ------------------------------------------------------------------ */

static void run_tests(void) {
    uint8_t cmd, val;
    unsigned i;

    cls();
    println("LINKTEST " VERSION " - HOST (mini-A end)");
    println("");

    /* --- reset (skip any stale replies left over from an aborted run) --- */
    bool ok = send_packet(CMD_RESET, 0);
    for (i = 0; ok; i++) {
        ok = recv_packet(&cmd, &val, TIMEOUT_TICKS);
        if (ok && cmd == REPLY(CMD_RESET))
            break;
        if (i >= 50)
            ok = false;
    }
    if (!ok) {
        println("FAIL: no answer from other calc.");
        println("Is LINKTEST running on it?");
        return;
    }
    println("Handshake OK.");

    /* --- ping test --- */
    printf_line("Ping test (%u round trips)...", PING_COUNT);
    unsigned long sum = 0, min = 0xFFFFFFFFUL, max = 0;
    unsigned bad = 0;
    for (i = 0; i < PING_COUNT; i++) {
        uint8_t v = (uint8_t)i;
        clock_t t0 = clock();
        if (!send_packet(CMD_PING, v) || !recv_packet(&cmd, &val, TIMEOUT_TICKS)) {
            printf_line("FAIL: timeout at ping %u", i);
            return;
        }
        unsigned long dt = (unsigned long)(clock() - t0);
        if (cmd != REPLY(CMD_PING) || val != v)
            bad++;
        sum += dt;
        if (dt < min) min = dt;
        if (dt > max) max = dt;
    }
    printf_line("  min %lu us", ticks_to_us(min));
    printf_line("  avg %lu us", ticks_to_us(sum) / PING_COUNT);
    printf_line("  max %lu us", ticks_to_us(max));
    printf_line("  wrong replies: %u", bad);

    /* --- exchange test --- */
    printf_line("Exchange test (%u bytes)...", EXCHANGE_COUNT);
    unsigned errors = 0;
    clock_t t0 = clock();
    for (i = 0; i < EXCHANGE_COUNT; i++) {
        if (!send_packet(CMD_EXCHANGE, host_byte(i)) ||
            !recv_packet(&cmd, &val, TIMEOUT_TICKS)) {
            printf_line("FAIL: timeout at byte %u", i);
            return;
        }
        if (cmd != REPLY(CMD_EXCHANGE) || val != device_byte(i))
            errors++;
    }
    unsigned long total = (unsigned long)(clock() - t0);
    unsigned long per_byte = ticks_to_us(total) / EXCHANGE_COUNT;
    printf_line("  %lu us per byte", per_byte);
    printf_line("  %lu bytes/sec", per_byte ? 1000000UL / per_byte : 0);
    printf_line("  errors here: %u", errors);

    /* --- tell device we're done, get its error count --- */
    if (send_packet(CMD_DONE, errors > 255 ? 255 : (uint8_t)errors) &&
        recv_packet(&cmd, &val, TIMEOUT_TICKS) && cmd == REPLY(CMD_DONE))
        printf_line("  errors on other calc: %u", val);
    else
        println("  (no final reply from other calc)");

    println("");
    println("Real Game Boy: ~1000 us per byte.");
    println("ENTER = run again, CLEAR = quit");
}

/* ------------------------------------------------------------------ */
/* Device side: answers packets                                        */
/* ------------------------------------------------------------------ */

static void device_loop(void) {
    uint8_t cmd, val;
    unsigned pings = 0, exchanged = 0, errors = 0;

    cls();
    println("LINKTEST " VERSION " - DEVICE (mini-B end)");
    println("");
    println("Waiting for host...");
    println("Press ENTER on the OTHER calc.");

    while (!user_quit) {
        if (!recv_packet(&cmd, &val, 0)) {
            if (user_quit || !connected())
                return;
            continue;
        }

        switch (cmd) {
            case CMD_RESET:
                pings = exchanged = errors = 0;
                send_packet(REPLY(CMD_RESET), 0);
                cls();
                println("LINKTEST " VERSION " - DEVICE (mini-B end)");
                println("");
                println("Test running...");
                break;

            case CMD_PING:
                send_packet(REPLY(CMD_PING), val);
                pings++;
                break;

            case CMD_EXCHANGE:
                if (val != host_byte(exchanged))
                    errors++;
                send_packet(REPLY(CMD_EXCHANGE), device_byte(exchanged));
                exchanged++;
                break;

            case CMD_DONE:
                send_packet(REPLY(CMD_DONE), errors > 255 ? 255 : (uint8_t)errors);
                printf_line("Pings answered: %u", pings);
                printf_line("Bytes exchanged: %u", exchanged);
                printf_line("Errors here: %u", errors);
                printf_line("Errors on host: %u", val);
                println("");
                println("Done! Results are on the host.");
                println("CLEAR = quit");
                break;

            default:
                break;
        }
    }
}

/* ------------------------------------------------------------------ */

int main(void) {
    gfx_Begin();
    gfx_SetTextFGColor(0);
    gfx_SetTextBGColor(255);
    gfx_SetTextTransparentColor(254);
    cls();

    println("LINKTEST " VERSION);
    println("");

    if (!link_Ready()) {
        println("USB init failed!");
        println("Press any key.");
        while (!os_GetCSC());
        gfx_End();
        return 0;
    }

    println("Connect cable + run LINKTEST on both.");
    println("CLEAR = quit");

    bool was_connected = false;

    while (!user_quit) {
        bool now = connected();

        if (now && !was_connected) {
            reset_link_state();
            if (link_IsHost()) {
                cls();
                println("LINKTEST " VERSION " - HOST (mini-A end)");
                println("");
                println("Connected to the other calc!");
                println("Press ENTER to start the test.");
                println("CLEAR = quit");
            } else {
                device_loop();
                now = connected();
                if (!now && !user_quit) {
                    cls();
                    println("Disconnected.");
                    println("Connect cable + run LINKTEST on both.");
                    println("CLEAR = quit");
                }
            }
        } else if (!now && was_connected) {
            reset_link_state();
            cls();
            println("Disconnected.");
            println("Connect cable + run LINKTEST on both.");
            println("CLEAR = quit");
        }
        was_connected = now;

        uint8_t key = os_GetCSC();
        if (key == sk_Clear || key == sk_Mode)
            break;
        if (key == sk_Enter && now && link_IsHost())
            run_tests();
    }

    gfx_End();
    return 0;
}
