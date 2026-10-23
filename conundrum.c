/*
 * conundrum-c
 * A C port of the original conundrum (Python 2.7) by Matthew K. Jensen.
 * Goal: identical output for the same command-line arguments and input data,
 * while being faster and more portable.
 *
 * License: GPL-2.0-only (same as the original)
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
  #define sleep_seconds(s) sleep(s)
#endif

#include "precon.h"

/* Extremely obvious flush interval – friendly size on modern platforms */
#define FLUSH_EVERY_BYTES 4096

#define VERSION "0.03ac-c"
#define MAX_SLEEP_ATTEMPTS 5
#define SPOTCHECK_INTERVAL_SEC 60

/* ---------- diagnostics helpers ---------- */

static void diag_timestamp(void)
{
    time_t now = time(NULL);
    char buf[64];
    struct tm *tm = localtime(&now);
    if (tm) {
        strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", tm);
        fprintf(stderr, "%s ", buf);
    }
}

static void diag(const char *msg)
{
    diag_timestamp();
    fprintf(stderr, "%s\n", msg);
}

/* ---------- argument parsing ---------- */

static int parse_hex16(const char *s, unsigned int *out)
{
    /* Expects strings that start with "0x" or "0X" followed by 1–4 hex digits */
    if (!s || (s[0] != '0') || (s[1] != 'x' && s[1] != 'X'))
        return 0;

    s += 2;
    if (*s == '\0')
        return 0;

    unsigned int val = 0;
    int digits = 0;
    while (*s) {
        char c = *s++;
        if (digits >= 4)
            return 0;
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
    int i;
    unsigned long long outcount = 0;
    int any_data = 0;
    time_t start_time, last_spotcheck;
    unsigned int bigwheelpos;
    int num_wheels;
    unsigned char *positions = NULL;   /* current position of each wheel */
    const unsigned char **cassette = NULL; /* pointers into WHEELS */
    size_t flush_counter = 0;

    start_time = time(NULL);
    last_spotcheck = start_time;

    fprintf(stderr, "Started conundrum-c %s with precon version %s and options:\n",
            VERSION, PRECONVER);
    for (i = 0; i < argc; i++)
        fprintf(stderr, "%s ", argv[i]);
    fprintf(stderr, "\n");
    diag("Startup diagnostics begin");

    /* ----- validate argument count ----- */
    if (argc < 3) {
        diag("Too few arguments. Need at least one wheel setting and the big-wheel position.");
        fprintf(stderr, "Usage: %s <wheelpos> [wheelpos ...] <bigwheelpos>\n", argv[0]);
        fprintf(stderr, "Each argument must be a 16-bit hex value starting with 0x\n");
        return 1;
    }

    num_wheels = argc - 2;   /* everything except program name and final big-wheel */

    /* ----- parse big-wheel position (last argument) ----- */
    if (!parse_hex16(argv[argc - 1], &bigwheelpos)) {
        fprintf(stderr, "Invalid big-wheel position: \"%s\"\n", argv[argc - 1]);
        diag("Aborting because of invalid big-wheel argument");
        return 1;
    }
    bigwheelpos &= 0xFFFF;

    fprintf(stderr, "bigwheelpos appears to be 0x%04x\n", bigwheelpos);
    fprintf(stderr, "cassette length is figured to be %d / 0x%x\n", num_wheels, num_wheels);

    /* ----- allocate and parse wheels ----- */
    cassette  = malloc((size_t)num_wheels * sizeof(*cassette));
    positions = malloc((size_t)num_wheels * sizeof(*positions));
    if (!cassette || !positions) {
        diag("Memory allocation failed for cassette/positions");
        free(cassette);
        free(positions);
        return 1;
    }

    for (i = 0; i < num_wheels; i++) {
        unsigned int setting;
        if (!parse_hex16(argv[i + 1], &setting)) {
            fprintf(stderr, "Invalid wheel setting: \"%s\"\n", argv[i + 1]);
            diag("Aborting because of invalid wheel argument");
            free(cassette);
            free(positions);
            return 1;
        }

        unsigned int wheel_id = (setting >> 8) & 0xFF;
        unsigned int pos      =  setting       & 0xFF;

        fprintf(stderr, "adding setting %04x with wheel %02x at position %02x\n",
                setting, wheel_id, pos);

        cassette[i]  = WHEELS[wheel_id];
        positions[i] = (unsigned char)pos;
    }

    diag("Moving on...");
    diag("starting intake loops");

    /* ----- first-byte special handling (short sleeps only once) ----- */
    int c = fgetc(stdin);
    if (c == EOF) {
        int attempt;
        for (attempt = 1; attempt <= MAX_SLEEP_ATTEMPTS; attempt++) {
            fprintf(stderr, "nextbyte empty before processing loops. Sleep interval currently at %d.\n",
                    attempt);
            sleep_seconds(attempt);
            c = fgetc(stdin);
            if (c != EOF)
                break;
        }
        if (c == EOF) {
            diag("Never got incoming bytes and hit interval limit.");
            free(cassette);
            free(positions);
            return 1;
        }
    }

    /* ----- main processing loop ----- */
    while (c != EOF) {
        unsigned char xr = 0;
        int w;

        /* XOR with every wheel at its current position */
        for (w = 0; w < num_wheels; w++)
            xr ^= cassette[w][positions[w]];

        /* XOR with big wheel */
        xr ^= BW[bigwheelpos];

        /* output the result */
        if (fputc(c ^ xr, stdout) == EOF) {
            diag("Error writing to stdout");
            free(cassette);
            free(positions);
            return 1;
        }

        outcount++;
        any_data = 1;
        flush_counter++;

        if (flush_counter >= FLUSH_EVERY_BYTES) {
            if (fflush(stdout) != 0) {
                diag("Error flushing stdout");
                free(cassette);
                free(positions);
                return 1;
            }
            flush_counter = 0;
        }

        /* advance wheel positions (last wheel is least-significant) */
        for (w = num_wheels - 1; w >= 0; w--) {
            positions[w]++;
            if (positions[w] != 0)   /* no carry */
                break;
        }

        /* advance big wheel */
        bigwheelpos = (bigwheelpos + 1) & 0xFFFF;

        /* spot-check roughly every minute */
        {
            time_t now = time(NULL);
            if (now - last_spotcheck >= SPOTCHECK_INTERVAL_SEC) {
                fprintf(stderr, "Spot check triggered\n");
                fprintf(stderr, "Current wheel positions: ");
                for (w = 0; w < num_wheels; w++)
                    fprintf(stderr, "%02x ", positions[w]);
                fprintf(stderr, "\nbigwheelpos looks like: 0x%04x\n", bigwheelpos);
                last_spotcheck = now;
            }
        }

        c = fgetc(stdin);
    }

    /* final flush */
    fflush(stdout);

    fprintf(stderr, "Intake loops appear to be done.\n");
    fprintf(stderr, "Final count appears to be %llu / 0x%llx\n",
            outcount, outcount);
    fprintf(stderr, "Wheel positions are: ");
    for (i = 0; i < num_wheels; i++)
        fprintf(stderr, "%02x ", positions[i]);
    fprintf(stderr, "\n");
    fprintf(stderr, "bigwheel at position 0x%04x\n", bigwheelpos);
    diag("Done.");

    free(cassette);
    free(positions);

    return any_data ? 0 : 1;
}