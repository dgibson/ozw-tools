//
// lsozw - Tool to list ZWave nodes
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
#include "Options.h"
#include "Manager.h"
#include "Driver.h"
#include "Node.h"
#include "Group.h"
#include "Notification.h"
#include "value_classes/ValueStore.h"
#include "value_classes/Value.h"
#include "value_classes/ValueBool.h"
#include "platform/Log.h"

#define OZW_CONFIG_DIR		"/etc/openzwave"
#define OZW_DEFAULT_DEV		"/dev/zwave"

using namespace OpenZWave;

// Global configuration
static string zwave_port = OZW_DEFAULT_DEV;
static int debug = 0;

static bool g_initFailed = false;

typedef struct {
	uint32 m_homeId;
	uint8 m_nodeId;
	list<ValueID> m_values;
} NodeInfo;

static list<NodeInfo *> g_nodes;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t initCond = PTHREAD_COND_INITIALIZER;

//-----------------------------------------------------------------------------
// <GetNodeInfo>
// Return the NodeInfo object associated with this notification
//-----------------------------------------------------------------------------
NodeInfo *GetNodeInfo(Notification const *n)
{
	uint32 const homeId = n->GetHomeId();
	uint8 const nodeId = n->GetNodeId();
	for (list<NodeInfo *>::iterator it = g_nodes.begin();
	     it != g_nodes.end(); ++it) {
		NodeInfo *nodeInfo = *it;
		if ((nodeInfo->m_homeId == homeId)
		    && (nodeInfo->m_nodeId == nodeId)) {
			return nodeInfo;
		}
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------
void OnNotification(Notification const *n, void *ctx)
{
	// Must do this inside a critical section to avoid conflicts with the main thread
	pthread_mutex_lock(&g_mutex);

	if (debug > 1) {
		Notification *nc = new Notification(*n);
		fprintf(stderr, "DEBUG: %s homeId=0x%08x nodeId=%d\n",
			nc->GetAsString().c_str(),
			n->GetHomeId(), n->GetNodeId());
	}

	switch (n->GetType()) {
	case Notification::Type_ValueAdded:
		if (NodeInfo *nodeInfo = GetNodeInfo(n)) {
			// Add the new value to our list
			nodeInfo->m_values.
				push_back(n->GetValueID());
		}
		break;

	case Notification::Type_ValueRemoved:
		if (NodeInfo *nodeInfo = GetNodeInfo(n)) {
			// Remove the value from out list
			for (list<ValueID>::iterator it
				     = nodeInfo->m_values.begin();
			     it != nodeInfo->m_values.end();
			     ++it) {
				if ((*it) ==
				    n->GetValueID()) {
					nodeInfo->m_values.
						erase(it);
					break;
				}
			}
		}
		break;

	case Notification::Type_ValueChanged:
		break;

	case Notification::Type_Group:
		break;

	case Notification::Type_NodeAdded:
		{
			// Add the new node to our list
			NodeInfo *nodeInfo = new NodeInfo();
			nodeInfo->m_homeId = n->GetHomeId();
			nodeInfo->m_nodeId = n->GetNodeId();
			g_nodes.push_back(nodeInfo);
			break;
		}

	case Notification::Type_NodeRemoved:
		{
			// Remove the node from our list
			uint32 const homeId = n->GetHomeId();
			uint8 const nodeId = n->GetNodeId();
			for (list<NodeInfo *>::iterator it =
			     g_nodes.begin(); it != g_nodes.end(); ++it) {
				NodeInfo *nodeInfo = *it;
				if ((nodeInfo->m_homeId == homeId)
				    && (nodeInfo->m_nodeId == nodeId)) {
					g_nodes.erase(it);
					delete nodeInfo;
					break;
				}
			}
			break;
		}

	case Notification::Type_NodeEvent:
		break;

	case Notification::Type_PollingDisabled:
		break;

	case Notification::Type_PollingEnabled:
		break;

	case Notification::Type_DriverReady:
		break;

	case Notification::Type_DriverFailed:
		g_initFailed = true;
		pthread_cond_broadcast(&initCond);
		break;

	case Notification::Type_AwakeNodesQueried:
	case Notification::Type_AllNodesQueried:
	case Notification::Type_AllNodesQueriedSomeDead:
		pthread_cond_broadcast(&initCond);
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
	fprintf(stderr, "lsozw [-d] [-p device]\n");
	exit(1);
}

void parse_options(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "dp:")) != -1) {
		switch (opt) {
		case 'd':
			debug++;
			break;
		case 'p':
			zwave_port = optarg;
			break;
		default:
			usage();
		}
	}
}

void list_one_node(Manager *mgr, NodeInfo *ni)
{
	uint32_t hid = ni->m_homeId;
	uint8_t nid = ni->m_nodeId;
	uint8_t controller_nid = mgr->GetControllerNodeId(hid);
	string node_type = mgr->GetNodeType(hid, nid);
	string manuf_name = mgr->GetNodeManufacturerName(hid, nid);
	string prod_name = mgr->GetNodeProductName(hid, nid);
	string name = mgr->GetNodeName(hid, nid);

	printf("%s%08x:%02x %s: %s %s [%s]\n",
	       controller_nid == nid ? "*" : " ", hid, nid,
	       node_type.c_str(), manuf_name.c_str(), prod_name.c_str(),
	       name.c_str());
}

//-----------------------------------------------------------------------------
// <main>
// Create the driver and then wait
//-----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	Manager *mgr;

	parse_options(argc, argv);

	// Create the OpenZWave Manager.
	// The first argument is the path to the config files (where the manufacturer_specific.xml file is located
	// The second argument is the path for saved Z-Wave network state and the log file.  If you leave it NULL 
	// the log file will appear in the program's working directory.
	Options::Create(OZW_CONFIG_DIR, "", "");
	Options::Get()->AddOptionInt("SaveLogLevel", LogLevel_Detail);
	Options::Get()->AddOptionInt("QueueLogLevel", LogLevel_Debug);
	Options::Get()->AddOptionBool("ConsoleOutput", false);
	Options::Get()->Lock();

	Manager::Create();
	mgr = Manager::Get();

	// Add a callback handler to the manager.  The second argument is a context that
	// is passed to the OnNotification method.  If the OnNotification is a method of
	// a class, the context would usually be a pointer to that class object, to
	// avoid the need for the notification handler to be a static.
	mgr->AddWatcher(OnNotification, NULL);

	// Add a Z-Wave Driver
	// Modify this line to set the correct serial port for your PC interface.
	if (strcasecmp(zwave_port.c_str(), "usb") == 0) {
		mgr->AddDriver("HID Controller",
			       Driver::ControllerInterface_Hid);
	} else {
		mgr->AddDriver(zwave_port);
	}

	if (debug)
		fprintf(stderr, "Scanning ZWave network... (debug = %d)\n",
			debug);

	// Now we just wait for either the AwakeNodesQueried or AllNodesQueried notification,
	// then write out the config file.
	// In a normal app, we would be handling notifications and building a UI for the user.
	pthread_mutex_lock(&g_mutex);
	pthread_cond_wait(&initCond, &g_mutex);
	pthread_mutex_unlock(&g_mutex);

	if (debug)
		fprintf(stderr, "Scan complete.\n");

	// Since the configuration file contains command class information that is only 
	// known after the nodes on the network are queried, wait until all of the nodes 
	// on the network have been queried (at least the "listening" ones) before
	// writing the configuration file.  (Maybe write again after sleeping nodes have
	// been queried as well.)
	if (g_initFailed) {
		fprintf(stderr, "Initialization failed\n");
		exit(1);
	}

	// We don't want any more updates
	mgr->RemoveWatcher(OnNotification, NULL);

	pthread_mutex_lock(&g_mutex);
	for (std::list<NodeInfo *>::const_iterator it = g_nodes.begin();
	     it != g_nodes.end();
	     it++) {
		list_one_node(mgr, *it);
	}
	pthread_mutex_unlock(&g_mutex);

	// program exit (clean up)
	Manager::Destroy();
	Options::Destroy();
	pthread_mutex_destroy(&g_mutex);

	return 0;
}
