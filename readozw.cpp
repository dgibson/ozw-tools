//
// readozw
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

#include "ozw_tools.h"

#define COMMAND_CLASS_METER	0x32

using namespace OpenZWave;

// Global configuration
static string zwave_port = OZW_DEFAULT_DEV;
static int verbose = 0;
static int debug = 0;
static uint32_t read_hid;
static uint8_t read_nid;
static uint8_t read_instance;
static uint8_t read_ccid;
static uint8_t read_index;

// Global state
static pthread_mutex_t g_mutex;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

static bool scanned = false;
static bool finished = false;
static bool failed = false;

static ValueID *read_vid;

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

//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------
void OnNotification(Notification const *n, void *ctx)
{
	uint8_t instance, ccid, index;

	pthread_mutex_lock(&g_mutex);

	switch (n->GetType()) {
	case Notification::Type_ValueRemoved:
		if (read_vid && (n->GetValueID() == *read_vid)) {
			error("Value removed");
			return;
		}
		break;

	case Notification::Type_ValueAdded:
		instance = n->GetValueID().GetInstance();
		ccid = n->GetValueID().GetCommandClassId();
		index = n->GetValueID().GetIndex();

		if ((n->GetHomeId() == read_hid)
		    && (n->GetNodeId() == read_nid)
		    && (instance == read_instance)
		    && (ccid == read_ccid)
		    && (index == read_index)) {
			pr_debug(1, "ValueID 0x%llx\n", n->GetValueID().GetId());
			read_vid = new ValueID(n->GetValueID());
		}
		break;

	case Notification::Type_ValueChanged:
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
	fprintf(stderr, "readozw [-p port] <home-id>:<node-id> <instance>,<command class>,<index>\n");
	exit(1);
}

void parse_options(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "dvp:")) != -1) {
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
		default:
			usage();
		}
	}

	if (argc != optind + 2)
		usage();

	if (!parse_znode(argv[optind], &read_hid, &read_nid))
		usage();

	if (!parse_vid(argv[optind + 1], &read_instance,
		       &read_ccid, &read_index))
		usage();	
}

static void read_value(Manager *mgr)
{
	assert(read_vid);
	ValueID vid = ValueID(*read_vid);
	string label = mgr->GetValueLabel(vid);
	string units = mgr->GetValueUnits(vid);
	string value;

	if (!mgr->GetValueAsString(vid, &value)) {
		error("Unable to read value");
		return;
	}

	if (verbose)
		printf("%s\t%s %s\n", label.c_str(), value.c_str(),
		       units.c_str());
	else
		printf("%s\n", value.c_str());
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
	while (!scanned && !read_vid) {
		pthread_cond_wait(&g_cond, &g_mutex);
	}

	if (scanned)
		pr_debug(1, "Z-Wave scan completed\n");

	if (!read_vid) {
		fprintf(stderr, "Couldn't find value to read\n");
		finished = true;
		failed = true;
	}

	read_value(mgr);

	pthread_mutex_unlock(&g_mutex);

	ozw_cleanup(mgr);

	pthread_mutex_destroy(&g_mutex);

	if (failed)
		exit(1);

	exit(0);
}
