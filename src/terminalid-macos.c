/*
    Copyright (C) 2014-2018 Flexible Software Solutions S.L.U.

    This file is part of flexVDI Client.

    flexVDI Client is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    flexVDI Client is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with flexVDI Client. If not, see <https://www.gnu.org/licenses/>.
*/

#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <glib.h>

gchar * discover_terminal_id() {
    struct ifaddrs * addresses, * addr;
    int mib[] = { CTL_NET, AF_ROUTE, 0, AF_LINK, NET_RT_IFLIST, 0 };
    size_t len;

    if (getifaddrs(&addresses) == 0) {
        for (addr = addresses; addr != NULL; addr = addr->ifa_next) {
            if (!(addr->ifa_flags & IFF_LOOPBACK)) { // do not count loopback devices
                if ((mib[5] = if_nametoindex(addr->ifa_name)) == 0 ||
                    sysctl(mib, 6, NULL, &len, NULL, 0) < 0) continue;

                g_autofree struct if_msghdr * ifm = g_malloc(len);
                sysctl(mib, 6, (char *)ifm, &len, NULL, 0);
                struct sockaddr_dl * sdl = (struct sockaddr_dl *)(ifm + 1);
                unsigned char * mac = (unsigned char *)LLADDR(sdl);
		if (!mac[0] && !mac[1] && !mac[2] && !mac[3] && !mac[4] && !mac[5]) continue;

                g_debug("terminal-id: using MAC address from iface %s", addr->ifa_name);
                freeifaddrs(addresses);
                return g_strdup_printf("%02x:%02x:%02x:%02x:%02x:%02x",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }
        }
        freeifaddrs(addresses);
    }

    g_debug("terminal-id: no MAC address found");
    return g_strdup("");
}
