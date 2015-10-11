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

string format_vid(const ValueID vid)
{
	return stringf("%016llx", vid.GetId());
}
