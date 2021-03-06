//
// ozw_tools - Common helper routines
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
#include <stdarg.h>
#include <assert.h>

#include "ozw_tools.h"

using namespace OpenZWave;

Manager *ozw_setup(const string port,
		   Manager::pfnOnNotification_t watcher, void *ctx)
{
	Manager *mgr;

	// Create the OpenZWave Manager.
	// The first argument is the path to the config files (where the manufacturer_specific.xml file is located
	// The second argument is the path for saved Z-Wave network state and the log file.  If you leave it NULL 
	// the log file will appear in the program's working directory.
	Options::Create(OZW_CONFIG_DIR, OZW_CACHE_DIR, "");
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
	mgr->AddWatcher(watcher, NULL);

	// Add a Z-Wave Driver
	// Modify this line to set the correct serial port for your PC interface.
	if (strcasecmp(port.c_str(), "usb") == 0) {
		mgr->AddDriver("HID Controller",
			       Driver::ControllerInterface_Hid);
	} else {
		mgr->AddDriver(port);
	}

	return mgr;
}

void ozw_cleanup(Manager *mgr)
{
	assert(mgr == Manager::Get());

	Manager::Destroy();
	Options::Destroy();
}

string stringf(const char *fmt, ...)
{
	va_list ap;
	char *tmp;

	va_start(ap, fmt);

	vasprintf(&tmp, fmt, ap);
	string s = std::string(tmp);

	free(tmp);
	va_end(ap);

	return s;
}

string format_znode(uint32_t hid, uint8_t nid)
{
	return stringf("%08x:%02x", hid, nid);
}

bool parse_znode(const string s, uint32_t *hidp, uint8_t *nidp)
{
	unsigned long long hid;
	unsigned long long nid;
	const char *p = s.c_str();
	char *ep;

	hid = strtoull(p, &ep, 16);
	if ((*ep != ':') || (hid & (~0xffffffffULL)))
		return false;

	nid = strtoull(ep + 1, &ep, 16);
	if ((*ep != '\0') || (nid & (~0xff)))
		return false;

	if (hidp)
		*hidp = hid;
	if (nidp)
		*nidp = nid;

	return true;
}

string format_vid(const ValueID vid)
{
	uint8_t instance = vid.GetInstance();
	uint8_t ccid = vid.GetCommandClassId();
	uint8_t index = vid.GetIndex();

	return stringf("%u,0x%x,%u", instance, ccid, index);
}

bool parse_vid(const string s,
	       uint8_t *instancep, uint8_t *ccidp, uint8_t *indexp)
{
	unsigned long long instance, ccid, index;
	const char *p = s.c_str();
	char *ep;

	instance = strtoull(p, &ep, 0);
	if ((*ep != ',') || (instance & (~0xffULL)))
		return false;

	ccid = strtoull(ep + 1, &ep, 0);
	if ((*ep != ',') || (ccid & (~0xffULL)))
		return false;

	index = strtoull(ep + 1, &ep, 0);
	if ((*ep != '\0') || (index & (~0xffULL)))
		return false;

	if (instancep)
		*instancep = instance;
	if (ccidp)
		*ccidp = ccid;
	if (indexp)
		*indexp = index;

	return true;
	
}

ValueMatcher::ValueMatcher(string nstr, string vstr)
{
	ok = true;

	if (!parse_znode(nstr, &hid, &nid))
		ok = false;

	if (!parse_vid(vstr, &instance, &ccid, &index))
		ok = false;
}

bool ValueMatcher::valid(void)
{
	return ok;
}

bool ValueMatcher::matches(OpenZWave::Notification const *n)
{
	if ((n->GetHomeId() == hid) && (n->GetNodeId() == nid)
	    && (n->GetValueID().GetInstance() == instance)
	    && (n->GetValueID().GetCommandClassId() == ccid)
	    && (n->GetValueID().GetIndex() == index)) {
		return true;
	}

	return false;
}
