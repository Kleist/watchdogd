/* Tool to query status, control, and verify watchdog functionality
 *
 * Copyright (c) 2016  Joachim Nilsson <troglobit@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "wdog.h"

#define OPT_T "t:"
#define log(fmt, args...) if (verbose) fprintf(stderr, fmt "\n", ##args)

extern char *__progname;
static int verbose = 0;
static int count = 1;
static int tmo   = 2000;	/* At least 2 sec timeout! */

static int false_ack = 0;
static int false_unsubscribe = 0;
static int disable_enable = 0;
static int no_kick = 0;
static int failed_kick = 0;
static int premature = 0;

static int do_clear(void)
{
	return wdog_reboot_reason_clr();
}

static int do_enable(int ena)
{
	int result = wdog_enable(ena);

	if (verbose) {
		int status = 0;

		wdog_status(&status);
		printf("%s\n", status ? "Enabled" : "Disabled");
	}

	return result;
}

static int do_reset(int msec)
{
	if (msec < 0)
		errx(1, "Invalid reboot timeout (%d)", msec);

	return wdog_reboot_timeout(getpid(), "*REBOOT*", msec);
}

static int set_loglevel(char *arg)
{
	int result = wdog_set_loglevel(arg);

	if (verbose)
		printf("loglevel: %s\n", wdog_get_loglevel());

	return result;
}

static int show_status(void)
{
	FILE *fp;
	char *file[] = {
		WDOG_STATUS,
		WDOG_STATE,
		NULL
	};

	for (int i = 0; file[i]; i++) {
		fp = fopen(file[i], "r");
		if (fp) {
			char buf[80];

			while (fgets(buf, sizeof(buf), fp))
				fputs(buf, stdout);

			fclose(fp);
		}
	}

	return 0;
}

static int show_version(void)
{
	return puts(PACKAGE_VERSION) != EOF;
}

static int testit(void)
{
	int id, ack;

	log("Verifying watchdog connectivity");
	if (wdog_pmon_ping())
		err(1, "Failed connectivity check");

	log("Subscribing to process supervisor");
	id = wdog_pmon_subscribe(NULL, tmo, &ack);
	if (id < 0) {
		perror("Failed connecting to pmon");
		return 1;
	}

	if (false_ack)
		ack += 42;
	if (false_unsubscribe) {
		ack += 42;
		count = 0;
	}
	if (disable_enable)
		count += 10;
	if (no_kick) {
		count = 0;
		usleep(tmo * 1000 * 5);
	}

	log("Starting test loop:\n"
	    "\tcount             : %d\n"
	    "\tfalse ack         : %d\n"
	    "\tfalse unsubscribe : %d\n"
	    "\tdisable enable    : %d\n"
	    "\tno kick           : %d\n"
	    "\tpremature trigger : %d\n", count, false_ack, false_unsubscribe,
	    disable_enable, no_kick, premature);

	while (count-- > 0) {
		log("Sleeping %d msec", tmo / 2);
		usleep(tmo / 2 * 1000);

		log("Kicking watchdog: id %d, ack %d", id, ack);
		if (wdog_pmon_kick(id, &ack))
			err(1, "Failed kicking");

		if (count == 8)
			wdog_enable(0);
		if (count == 4)
			wdog_enable(1);
		if (failed_kick)
			ack += 42;
		if (premature)
			/* => 2000 / 2 * 1000 - 500000 = 500 ms */
			usleep(tmo / 2 * 1000 - 500000);
	}

	log("Unsubscribing: id %d, ack %d", id, ack);
	if (wdog_pmon_unsubscribe(id, ack))
		err(1, "Failed unsubscribe");

	return 0;
}

static int run_test(char *arg)
{
	int op = -1;
	struct { char *arg; int op; } opts[] = {
		{ "complete-cycle",    200 },
		{ "disable-enable",    201 },
		{ "false-ack",         202 },
		{ "false-unsubscribe", 203 },
		{ "failed-kick",       204 },
		{ "no-kick",           205 },
		{ "premature-trigger", 206 },
		{ NULL, 0 }
	};

	for (int i = 0; opts[i].arg; i++) {
		if (!strcmp(opts[i].arg, arg)) {
			op = opts[i].op;
			break;
		}
	}

	switch (op) {
		case 200:
			return testit();

		case 201:
			disable_enable = 1;
			return testit();

		case 202:
			false_ack = 1;
			return testit();

		case 203:
			false_unsubscribe = 1;
			return testit();

		case 204:
			failed_kick = 1;
			return testit();

		case 205:
			no_kick = 1;
			return testit();

		case 206:
			premature = 1;
			return testit();
	}

	return -1;
}

static int usage(int code)
{
	printf("Usage:\n"
	       "  %s [-cdefhsvV] [-l LEVEL] [-r MSEC]\n"
	       "\n"
	       "Options:\n"
	       "  -h, --help         Display this help text and exit\n"
	       "  -v, --verbose      Verbose output, otherwise coommands are silent\n"
	       "  -V, --version      Show program version\n"
	       "\n"
	       "Commands:\n"
	       "  clear              Clear reset reason\n"
	       "  debug              Toggle debug log level in daemon\n"
	       "  disable            Disable watchdog\n"
	       "  enable             Re-enable watchdog\n"
//	       "  force-reset        Forced reset, alias to `reboot 0`\n"
	       "  log LVL            Adjust log level: none, err, warn, notice*, info, debug\n"
	       "  reboot  [MSEC]     Reboot, with optional MSEC (milliseconds) delay\n"
	       "  status             Show watchdog and supervisor status\n"
	       "  test    [TEST]     Run built-in process monitor (PMON) test, see below\n"
	       "\n"
	       "Tests:\n"
	       "  complete-cycle     Verify subscribe, kick, and unsubscribe (no reboot)\n"
	       "  disable-enable     Verify WDT disable, and re-enable (no reboot)\n"
	       "  false-ack          Verify kick with invalid ACK (reboot)\n"
	       "  false-unsubscribe  Verify unsubscribe with invalid ACK (reboot)\n"
	       "  failed-kick        Verify reboot on missing kick (reboot)\n"
	       "  no-kick            Verify reboot on missing first kick (reboot)\n"
	       "  premature-trigger  Verify no premature trigger before unsubscribe (reboot)\n"
	       "\n", __progname);

	return code;
}

int main(int argc, char *argv[])
{
	int c, result = 0;
	char *test = NULL;
	struct option long_options[] = {
		/* Options/Commands */
		{ "clear",             0, 0, 'c' },
		{ "disable",           0, 0, 'd' },
		{ "enable",            0, 0, 'e' },
		{ "force-reset",       0, 0, 'f' },
		{ "loglevel",          1, 0, 'l' },
		{ "help",              0, 0, 'h' },
		{ "reboot",            1, 0, 'r' },
		{ "status",            0, 0, 's' },
		{ "test",              1, 0, 't' },
		{ "verbose",           0, 0, 'V' },
		{ "version",           0, 0, 'v' },
		{ NULL, 0, 0, 0 }
	};

	while ((c = getopt_long(argc, argv, "cdefl:hr:sVv?" OPT_T, long_options, NULL)) != EOF) {
		switch (c) {
		case 'c':
			result += do_clear();
			break;

		case 'd':
			result += do_enable(0);
			break;

		case 'e':
			result += do_enable(1);
			break;

		case 'f':
			result += do_reset(0);
			break;

		case 'l':
			result += set_loglevel(optarg);
			break;

		case 'h':
			result += usage(0);
			break;

		case 'r':
			result += do_reset(atoi(optarg));
			break;

		case 's':
			result += show_status();
			break;

		case 't':
			test = optarg;
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			result += show_version();
			break;

		default:
			warnx("Unknown or currently unsupported option '%c'.", c);
			return usage(1);
		}
	}

	if (test)
		result += run_test(test);

	return result;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
