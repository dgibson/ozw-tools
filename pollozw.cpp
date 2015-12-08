//
// pollozw
//
// Copyright David Gibson 2015 <ozw@gibson.dropbear.id.au>
//
// Based on the MinOZW code shipped with OpenZWave:
//     Copyright (c) 2010 Mal Lansell <mal@openzwave.com>
//
// This program is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see
// <http://www.gnu.org/licenses/>.
//

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <time.h>

#include "ozw_tools.h"

#define DEFAULT_INTERVAL	10

using namespace OpenZWave;

// Global configuration
static string zwave_port = OZW_DEFAULT_DEV;
static int verbose = 0;
static int debug = 0;
static unsigned long interval = DEFAULT_INTERVAL;
static list<ValueMatcher *> matchlist;
static string time_fmt = "%c";
static bool use_utc = false;

// Global state
static pthread_mutex_t g_mutex;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

static bool scanned = false;
static bool finished = false;
static bool failed = false;

class ValueInfo {
};

static map<ValueID, ValueInfo *> vidmap;

static void pr_debug(int level, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

static void pr_debug(int level, const char *fmt, ...)
{
	va_list ap;

	if (debug < level)
		return;

	pthread_mutex_lock(&g_mutex);

	va_start(ap, fmt);
	fprintf(stderr, "DEBUG: ");
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	pthread_mutex_unlock(&g_mutex);
}

static void error(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));

static void error(const char *fmt, ...)
{
	va_list ap;

	pthread_mutex_lock(&g_mutex);

	va_start(ap, fmt);
	fprintf(stderr, "ERROR: ");
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	finished = true;
	failed = true;
	pthread_cond_broadcast(&g_cond);

	pthread_mutex_unlock(&g_mutex);
}

static void print_value(Manager *mgr, ValueID vid)
{
	time_t now;
	struct tm *now_tm;
	char timestr[128];
	string label = mgr->GetValueLabel(vid);
	string units = mgr->GetValueUnits(vid);
	string value;

	if (!mgr->GetValueAsString(vid, &value)) {
		error("Unable to read value");
		return;
	}

	now = time(NULL);
	if (use_utc)
		now_tm = gmtime(&now);
	else
		now_tm = localtime(&now);
	strftime(timestr, sizeof(timestr), time_fmt.c_str(), now_tm);

	if (verbose)
		printf("%s\t%s\t%s %s\n", timestr, label.c_str(),
		       value.c_str(), units.c_str());
	else
		printf("%s\t%s\n", timestr, value.c_str());
}

//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------
void OnNotification(Notification const *n, void *ctx)
{
	Manager *mgr = Manager::Get();
	pthread_mutex_lock(&g_mutex);

	switch (n->GetType()) {
	case Notification::Type_ValueRemoved:
		vidmap.erase(n->GetValueID());
		break;

	case Notification::Type_ValueAdded:
		for (list<ValueMatcher *>::iterator it = matchlist.begin();
		     it != matchlist.end(); it++) {
			if ((*it)->matches(n)) {
				vidmap[n->GetValueID()] = new ValueInfo();
			}
		}
		break;

	case Notification::Type_ValueChanged:
		if (!scanned)
			/* only start polling once we've completed the scan */
			break;
		if (vidmap.count(n->GetValueID()))
			print_value(mgr, n->GetValueID());
		break;

	case Notification::Type_Group:
		break;

	case Notification::Type_NodeAdded:
		break;

	case Notification::Type_NodeRemoved:
		break;

	case Notification::Type_NodeEvent:
		break;

	case Notification::Type_PollingDisabled:
		break;

	case Notification::Type_PollingEnabled:
		break;

	case Notification::Type_DriverReady:
		break;

	case Notification::Type_DriverFailed:
		error("Driver failed");
		scanned = true;
		failed = true;
		break;

	case Notification::Type_AwakeNodesQueried:
	case Notification::Type_AllNodesQueried:
	case Notification::Type_AllNodesQueriedSomeDead:
		scanned = true;
		pthread_cond_broadcast(&g_cond);
		break;

	case Notification::Type_DriverReset:
	case Notification::Type_Notification:
	case Notification::Type_NodeNaming:
	case Notification::Type_NodeProtocolInfo:
	case Notification::Type_NodeQueriesComplete:
	default:
		break;
	}

	pthread_mutex_unlock(&g_mutex);
}

void usage(void)
{
	fprintf(stderr,
		"pollozw [-p port] [-i interval] [-f time format] [-u]\n"
		"        {<home-id>:<node-id> <instance>,<command class>,<index>}...\n");
	exit(1);
}

void parse_options(int argc, char *argv[])
{
	char *ep;
	int opt;
	int i;

	while ((opt = getopt(argc, argv, "dvp:i:f:u")) != -1) {
		switch (opt) {
		case 'd':
			debug++;
			break;
		case 'v':
			verbose++;
			break;
		case 'p':
			zwave_port = optarg;
			break;
		case 'i':
			interval = strtoul(optarg, &ep, 0);
			if (*ep)
				usage();
			break;
		case 'f':
			time_fmt = optarg;
			break;
		case 'u':
			use_utc = true;
			break;
		default:
			usage();
		}
	}

	if ((argc - optind) % 2)
		usage();

	for (i = optind; i  < argc; i += 2) {
		matchlist.push_back(new ValueMatcher(argv[i], argv[i + 1]));
	}
}

//-----------------------------------------------------------------------------
// <main>
// Create the driver and then wait
//-----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	Manager *mgr;
	pthread_mutexattr_t mutexattr;

	pthread_mutexattr_init(&mutexattr);
	pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&g_mutex, &mutexattr);
	pthread_mutexattr_destroy(&mutexattr);

	parse_options(argc, argv);

	mgr = ozw_setup(zwave_port, OnNotification);

	pr_debug(1, "Scanning Z-Wave network\n");

	pthread_mutex_lock(&g_mutex);
	while (!scanned) {
		pthread_cond_wait(&g_cond, &g_mutex);
	}

	if (!failed) {
		pr_debug(1, "Z-Wave scan completed\n");

		pr_debug(1, "Poll interval %lus\n", interval);
		mgr->SetPollInterval(interval * 1000, false);

		for (map<ValueID, ValueInfo *>::iterator it = vidmap.begin();
		     it != vidmap.end(); it++) {
			mgr->EnablePoll(it->first);
		}

		while (!failed) {
			pthread_cond_wait(&g_cond, &g_mutex);
		}
	}

	pthread_mutex_unlock(&g_mutex);

	ozw_cleanup(mgr);

	pthread_mutex_destroy(&g_mutex);

	if (failed)
		exit(1);

	exit(0);
}
