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

static uint32 g_homeId = 0;
static bool g_initFailed = false;

typedef struct {
	uint32 m_homeId;
	uint8 m_nodeId;
	list<ValueID> m_values;
} NodeInfo;

static list<NodeInfo *> g_nodes;
static pthread_mutex_t g_criticalSection;
static pthread_cond_t initCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

//-----------------------------------------------------------------------------
// <GetNodeInfo>
// Return the NodeInfo object associated with this notification
//-----------------------------------------------------------------------------
NodeInfo *GetNodeInfo(Notification const *_notification)
{
	uint32 const homeId = _notification->GetHomeId();
	uint8 const nodeId = _notification->GetNodeId();
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
void OnNotification(Notification const *_notification, void *_context)
{
	// Must do this inside a critical section to avoid conflicts with the main thread
	pthread_mutex_lock(&g_criticalSection);

	switch (_notification->GetType()) {
	case Notification::Type_ValueAdded:
		if (NodeInfo *nodeInfo = GetNodeInfo(_notification)) {
			// Add the new value to our list
			nodeInfo->m_values.
				push_back(_notification->GetValueID());
		}
		break;

	case Notification::Type_ValueRemoved:
		if (NodeInfo *nodeInfo = GetNodeInfo(_notification)) {
			// Remove the value from out list
			for (list<ValueID>::iterator it
				     = nodeInfo->m_values.begin();
			     it != nodeInfo->m_values.end();
			     ++it) {
				if ((*it) ==
				    _notification->GetValueID()) {
					nodeInfo->m_values.
						erase(it);
					break;
				}
			}
		}
		break;

	case Notification::Type_ValueChanged:
		// One of the node values has changed
		if (NodeInfo *nodeInfo = GetNodeInfo(_notification)) {
			nodeInfo = nodeInfo;	// placeholder for real action
		}
		break;

	case Notification::Type_Group:
		// One of the node's association groups has changed
		if (NodeInfo *nodeInfo = GetNodeInfo(_notification)) {
			nodeInfo = nodeInfo;	// placeholder for real action
		}
		break;

	case Notification::Type_NodeAdded:
		{
			// Add the new node to our list
			NodeInfo *nodeInfo = new NodeInfo();
			nodeInfo->m_homeId = _notification->GetHomeId();
			nodeInfo->m_nodeId = _notification->GetNodeId();
			g_nodes.push_back(nodeInfo);
			break;
		}

	case Notification::Type_NodeRemoved:
		{
			// Remove the node from our list
			uint32 const homeId = _notification->GetHomeId();
			uint8 const nodeId = _notification->GetNodeId();
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
		// We have received an event from the node, caused by a
		// basic_set or hail message.
		if (NodeInfo *nodeInfo = GetNodeInfo(_notification)) {
			nodeInfo = nodeInfo;	// placeholder for real action
		}
		break;

	case Notification::Type_PollingDisabled:
		if (NodeInfo *nodeInfo = GetNodeInfo(_notification)) {
			nodeInfo->m_polled = false;
		}
		break;

	case Notification::Type_PollingEnabled:
		if (NodeInfo *nodeInfo = GetNodeInfo(_notification)) {
			nodeInfo->m_polled = true;
		}
		break;

	case Notification::Type_DriverReady:
		g_homeId = _notification->GetHomeId();
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

	pthread_mutex_unlock(&g_criticalSection);
}

//-----------------------------------------------------------------------------
// <main>
// Create the driver and then wait
//-----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	pthread_mutexattr_t mutexattr;

	pthread_mutexattr_init(&mutexattr);
	pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&g_criticalSection, &mutexattr);
	pthread_mutexattr_destroy(&mutexattr);

	pthread_mutex_lock(&initMutex);

	// Create the OpenZWave Manager.
	// The first argument is the path to the config files (where the manufacturer_specific.xml file is located
	// The second argument is the path for saved Z-Wave network state and the log file.  If you leave it NULL 
	// the log file will appear in the program's working directory.
	Options::Create(OZW_CONFIG_DIR, "", "");
	Options::Get()->AddOptionInt("SaveLogLevel", LogLevel_Detail);
	Options::Get()->AddOptionInt("QueueLogLevel", LogLevel_Debug);
	Options::Get()->AddOptionInt("DumpTrigger", LogLevel_Error);
	Options::Get()->AddOptionBool("ValidateValueChanges", true);
	Options::Get()->AddOptionBool("ConsoleOutput", false);
	Options::Get()->Lock();

	Manager::Create();

	// Add a callback handler to the manager.  The second argument is a context that
	// is passed to the OnNotification method.  If the OnNotification is a method of
	// a class, the context would usually be a pointer to that class object, to
	// avoid the need for the notification handler to be a static.
	Manager::Get()->AddWatcher(OnNotification, NULL);

	// Add a Z-Wave Driver
	// Modify this line to set the correct serial port for your PC interface.

	string port = OZW_DEFAULT_DEV;

	if (argc > 1) {
		port = argv[1];
	}
	if (strcasecmp(port.c_str(), "usb") == 0) {
		Manager::Get()->AddDriver("HID Controller",
					  Driver::ControllerInterface_Hid);
	} else {
		Manager::Get()->AddDriver(port);
	}

	// Now we just wait for either the AwakeNodesQueried or AllNodesQueried notification,
	// then write out the config file.
	// In a normal app, we would be handling notifications and building a UI for the user.
	pthread_cond_wait(&initCond, &initMutex);

	// Since the configuration file contains command class information that is only 
	// known after the nodes on the network are queried, wait until all of the nodes 
	// on the network have been queried (at least the "listening" ones) before
	// writing the configuration file.  (Maybe write again after sleeping nodes have
	// been queried as well.)
	if (g_initFailed) {
		fprintf(stderr, "Initialization failed\n");
		exit(1);
	}
	// program exit (clean up)
	if (strcasecmp(port.c_str(), "usb") == 0) {
		Manager::Get()->RemoveDriver("HID Controller");
	} else {
		Manager::Get()->RemoveDriver(port);
	}
	Manager::Get()->RemoveWatcher(OnNotification, NULL);
	Manager::Destroy();
	Options::Destroy();

	pthread_mutex_lock(&g_criticalSection);
	for (std::list<NodeInfo *>::const_iterator it = g_nodes.begin();
	     it != g_nodes.end();
	     it++) {
		NodeInfo *ni = *it;

		printf("Home ID 0x%08x, node %d\n", ni->m_homeId, ni->m_nodeId);
	}
	pthread_mutex_unlock(&g_criticalSection);

	pthread_mutex_destroy(&g_criticalSection);
	return 0;
}
