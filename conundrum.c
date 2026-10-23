/*
 * conundrum-c
 * C port of conundrum v0.03adt by Matthew K. Jensen
 * Goal: identical stdout output, much higher speed
 *
 * License: GPL-2.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
  #include <windows.h>
  #define sleep_seconds(s) Sleep((DWORD)(s) * 1000)
#else
  #include <unistd.h>
  #define sleep_seconds(s) sleep((unsigned int)(s))
#endif

#include "precon.h"

/* Extremely obvious flush interval */
#define FLUSH_EVERY_BYTES 4096

#define VERSION           "0.03adt-c"
#define MAX_SLEEP_ATTEMPTS 5
#define SPOTCHECK_INTERVAL_SEC 60

/* ---------- human-readable size (equivalent of precon.convert_size) ---------- */

static void print_human_size(unsigned long long bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int unit = 0;
    double size = (double)bytes;

    while (size >= 1024.0 && unit < 5) {
        size /= 1024.0;
        unit++;
    }

    if (unit == 0)
        fprintf(stderr, "%llu %s", bytes, units[unit]);
    else
        fprintf(stderr, "%.2f %s", size, units[unit]);
}

/* ---------- helpers ---------- */

static void diag_time(void)
{
    time_t now = time(NULL);
    char buf[64];
    struct tm *tm = localtime(&now);
    if (tm) {
        strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", tm);
        fprintf(stderr, "%s ", buf);
    }
}

static int parse_hex16(const char *s, unsigned int *out)
{
    if (!s || s[0] != '0' || (s[1] != 'x' && s[1] != 'X'))
        return 0;

    s += 2;
    if (*s == '\0')
        return 0;

    unsigned int val = 0;
    int digits = 0;

    while (*s) {
        char c = *s++;
        if (digits >= 4)
            return 0;               /* more than 4 hex digits → reject */
        if (c >= '0' && c <= '9')
            val = (val << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f')
            val = (val << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            val = (val << 4) | (c - 'A' + 10);
        else
            return 0;
        digits++;
    }

    *out = val;
    return 1;
}

/* ---------- main ---------- */

int main(int argc, char *argv[])
{
    time_t start_time = time(NULL);
    time_t last_spotcheck = start_time;
    unsigned long long outcount = 0;
    int any_data = 0;
    size_t flush_counter = 0;

    unsigned int bigwheelpos;
    int num_wheels;
    unsigned char *positions = NULL;
    unsigned char *original_positions = NULL;
    const unsigned char **cassette = NULL;

    int i, w;
    int c;

    /* ---- startup banner ---- */
    fprintf(stderr, "#### Started conundrum-c %s with precon version %s and options:\n",
            VERSION, PRECONVER);
    for (i = 0; i < argc; i++)
        fprintf(stderr, "%s ", argv[i]);
    fprintf(stderr, "\nat ");
    diag_time();
    fprintf(stderr, "\n");

    /* ---- argument count ---- */
    if (argc < 3) {
        fprintf(stderr, "Too few arguments. Need at least one wheel setting and the big-wheel position.\n");
        fprintf(stderr, "Usage: %s <wheelpos> [wheelpos ...] <bigwheelpos>\n", argv[0]);
        return 1;
    }

    num_wheels = argc - 2;

    /* ---- parse big-wheel (last argument) ---- */
    if (!parse_hex16(argv[argc - 1], &bigwheelpos)) {
        fprintf(stderr, "Invalid big-wheel position: \"%s\"\n", argv[argc - 1]);
        return 1;
    }
    bigwheelpos &= 0xFFFF;

    fprintf(stderr, "bigwheelpos appears to be 0x%04x\n", bigwheelpos);
    fprintf(stderr, "cassette length is figured to be %d / 0x%x\n", num_wheels, num_wheels);
    fprintf(stderr, "#### Processing settings\n");

    /* ---- allocate ---- */
    cassette           = malloc((size_t)num_wheels * sizeof(*cassette));
    positions          = malloc((size_t)num_wheels * sizeof(*positions));
    original_positions = malloc((size_t)num_wheels * sizeof(*original_positions));

    if (!cassette || !positions || !original_positions) {
        fprintf(stderr, "Memory allocation failed\n");
        free(cassette);
        free(positions);
        free(original_positions);
        return 1;
    }

    /* ---- parse wheels ---- */
    for (i = 0; i < num_wheels; i++) {
        unsigned int setting;
        if (!parse_hex16(argv[i + 1], &setting)) {
            fprintf(stderr, "Invalid wheel setting: \"%s\"\n", argv[i + 1]);
            free(cassette);
            free(positions);
            free(original_positions);
            return 1;
        }

        unsigned int wheel_id = (setting >> 8) & 0xFF;
        unsigned int pos      =  setting       & 0xFF;

        fprintf(stderr, "adding setting %04x with wheel %02x at position %02x\n",
                setting, wheel_id, pos);

        cassette[i]  = WHEELS[wheel_id];
        positions[i] = (unsigned char)pos;
        original_positions[i] = positions[i];
    }

    fprintf(stderr, "Moving on...\n");
    fprintf(stderr, "#### Attempting to read from stdin at ");
    diag_time();
    fprintf(stderr, "\n");

    /* ---- first-byte delay handling ---- */
    c = fgetc(stdin);
    if (c == EOF) {
        int attempt;
        for (attempt = 1; attempt <= MAX_SLEEP_ATTEMPTS; attempt++) {
            fprintf(stderr, "### nextbyte empty before processing loops at ");
            diag_time();
            fprintf(stderr, "\nSleep interval currently at %d.\n", attempt);
            sleep_seconds(attempt);
            c = fgetc(stdin);
            if (c != EOF)
                break;
        }
        if (c == EOF) {
            fprintf(stderr, "Never got incoming bytes and hit interval limit.\n");
            free(cassette);
            free(positions);
            free(original_positions);
            return 1;
        }
    }

    fprintf(stderr, "#### Starting intake loops at ");
    diag_time();
    fprintf(stderr, "\n");

    /* ---- main processing loop ---- */
    while (c != EOF) {
        unsigned char xr = 0;

        for (w = 0; w < num_wheels; w++)
            xr ^= cassette[w][positions[w]];

        xr ^= BW[bigwheelpos];

        if (fputc(c ^ xr, stdout) == EOF) {
            fprintf(stderr, "Error writing to stdout\n");
            free(cassette);
            free(positions);
            free(original_positions);
            return 1;
        }

        outcount++;
        any_data = 1;
        flush_counter++;

        if (flush_counter >= FLUSH_EVERY_BYTES) {
            if (fflush(stdout) != 0) {
                fprintf(stderr, "Error flushing stdout\n");
                free(cassette);
                free(positions);
                free(original_positions);
                return 1;
            }
            flush_counter = 0;
        }

        /* advance positions (last wheel = least significant) */
        for (w = num_wheels - 1; w >= 0; w--) {
            positions[w]++;
            if (positions[w] != 0)
                break;
        }

        /* safety check: have we rolled all the way back? */
        if (outcount > 1 &&
            memcmp(positions, original_positions, (size_t)num_wheels) == 0) {

            fprintf(stderr,
                "################################################################################\n"
                "#                                                                              #\n"
                "#                                  WARNING!!!                                  #\n"
                "#                                                                              #\n"
                "# The wheel positions have rolled back to original values. The data is         #\n"
                "# therefore insecure. Conundrum must stop.                                     #\n"
                "#                                                                              #\n"
                "################################################################################\n\n");

            fprintf(stderr, "Time running before break: %.0f seconds.\n",
                    difftime(time(NULL), start_time));
            fprintf(stderr, "Total bytes: %llu bytes / ", outcount);
            print_human_size(outcount);
            fprintf(stderr, " before break. Godspeed.\n");
            fprintf(stderr, "Break at ");
            diag_time();
            fprintf(stderr, "\n");

            free(cassette);
            free(positions);
            free(original_positions);
            return 1;
        }

        bigwheelpos = (bigwheelpos + 1) & 0xFFFF;

        /* spot check roughly every minute */
        {
            time_t now = time(NULL);
            if (now - last_spotcheck >= SPOTCHECK_INTERVAL_SEC) {
                fprintf(stderr, "### Spot check at ");
                diag_time();
                fprintf(stderr, " with %llu bytes / ", outcount);
                print_human_size(outcount);
                fprintf(stderr, "\npositions are: ");
                for (w = 0; w < num_wheels; w++)
                    fprintf(stderr, "%02x ", positions[w]);
                fprintf(stderr, "and bigwheelpos looks like: 0x%04x\n", bigwheelpos);
                fflush(stderr);
                last_spotcheck = now;
            }
        }

        c = fgetc(stdin);
    }

    /* final flush */
    fflush(stdout);

    /* ---- final summary ---- */
    fprintf(stderr, "#### Intake loops appear to be done at ");
    diag_time();
    fprintf(stderr, "\n");

    fprintf(stderr, "Wheel positions are: ");
    for (i = 0; i < num_wheels; i++)
        fprintf(stderr, "%02x ", positions[i]);
    fprintf(stderr, "\n");

    fprintf(stderr, "Time running: %.0f seconds.\n", difftime(time(NULL), start_time));
    fprintf(stderr, "Total bytes: %llu / ", outcount);
    print_human_size(outcount);
    fprintf(stderr, "\n");

    fprintf(stderr, "%s Start time.\n", ctime(&start_time));
    fprintf(stderr, "Done.\nPeace, strength, and wisdom to you.\n");

    free(cassette);
    free(positions);
    free(original_positions);

    return any_data ? 0 : 1;
}
