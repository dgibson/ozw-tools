//
// ozw-power-meter-log
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
enum {
	LOG_ERROR = 0,
	LOG_INFO,
	LOG_DEBUG,
};
static int loglevel = LOG_DEBUG;

// Global state
static pthread_mutex_t g_mutex;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

static bool scanned = false;
static bool finished = false;
static bool failed = false;

struct ValueInfo {
	string name;
};

static map<ValueID, ValueInfo *> valmap;

static void log(int level, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

static void log(int level, const char *fmt, ...)
{
	va_list ap;

	if (loglevel < level)
		return;

	pthread_mutex_lock(&g_mutex);

	va_start(ap, fmt);
	fprintf(stderr, "ozw-power-meter-log: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);

	pthread_mutex_unlock(&g_mutex);
}

static ValueInfo *value_info(Manager *mgr, ValueID vid)
{
	string label;
	struct ValueInfo *vinfo;

	if (vid.GetCommandClassId() != COMMAND_CLASS_METER)
		return NULL;

	label = mgr->GetValueLabel(vid);

	if (label != "Energy" && label != "Power")
		return NULL;

	vinfo = new ValueInfo;
	vinfo->name = label;
	return vinfo;
}

void value_update(Manager *mgr, ValueID vid, ValueInfo *vinfo)
{
	string val, units;

	if (!mgr->GetValueAsString(vid, &val)) {
		log(LOG_ERROR, "Error retrieving %s", vinfo->name.c_str());
		return;
	}

	units = mgr->GetValueUnits(vid);
	
	log(LOG_INFO, "%s: %s %s", vinfo->name.c_str(),
	    val.c_str(), units.c_str());
}

//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------
void OnNotification(Notification const *n, void *ctx)
{
	Manager *mgr = Manager::Get();
	ValueInfo *vinfo;
	map<ValueID, ValueInfo *>::const_iterator it;

	pthread_mutex_lock(&g_mutex);

	switch (n->GetType()) {
	case Notification::Type_ValueRemoved:
		if (valmap.find(n->GetValueID()) == valmap.end())
			break;

		log(LOG_INFO, "Removing tracked value 0x%llx",
		    n->GetValueID().GetId());
		valmap.erase(n->GetValueID());
		break;

	case Notification::Type_ValueAdded:
		vinfo = value_info(mgr, n->GetValueID());
		if (!vinfo)
			break;

		log(LOG_DEBUG, "Polling value 0x%llx %s",
		    n->GetValueID().GetId(), vinfo->name.c_str());
		valmap.insert(pair<ValueID, ValueInfo*>(n->GetValueID(), vinfo));

		/* FALLTHROUGH */

	case Notification::Type_ValueChanged:
		it = valmap.find(n->GetValueID());
		if (it == valmap.end())
			break;

		vinfo = it->second;
		value_update(mgr, n->GetValueID(), vinfo);
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
		log(LOG_INFO, "Polling disabled on %08x:%02x",
		    n->GetHomeId(), n->GetNodeId());
		break;

	case Notification::Type_PollingEnabled:
		log(LOG_INFO, "Polling enabled on %08x:%02x",
		    n->GetHomeId(), n->GetNodeId());
		break;

	case Notification::Type_DriverReady:
		break;

	case Notification::Type_DriverFailed:
		failed = true;
		finished = true;
		pthread_cond_broadcast(&g_cond);
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
	fprintf(stderr, "ozw-power-meter-log [-p port]\n");
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

	log(LOG_INFO, "Starting");

	pthread_mutex_lock(&g_mutex);
	while (!scanned) {
		pthread_cond_wait(&g_cond, &g_mutex);
	}

	log(LOG_INFO, "Initial Z-Wave scan completed");

	for (map<ValueID, ValueInfo *>::const_iterator it = valmap.begin();
	     it != valmap.end(); it++) {
		ValueID vid = it->first;

		mgr->EnablePoll(vid, 1);
	}

	while (!finished) {
		pthread_cond_wait(&g_cond, &g_mutex);
	}
	pthread_mutex_unlock(&g_mutex);

	log(LOG_INFO, "Terminating");

	ozw_cleanup(mgr);

	pthread_mutex_destroy(&g_mutex);

	if (failed)
		exit(1);

	exit(0);
}
