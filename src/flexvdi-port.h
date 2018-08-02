/**
 * Copyright Flexible Software Solutions S.L. 2014
 **/

#ifndef _FLEXVDI_PORT_H_
#define _FLEXVDI_PORT_H_

#include <glib.h>
#include "flexdp.h"

void flexvdi_port_register_session(gpointer session);
int flexvdi_is_agent_connected(void);
typedef void (*flexvdi_agent_connected_cb)(gpointer data);
void flexvdi_on_agent_connected(flexvdi_agent_connected_cb cb, gpointer data);
int flexvdi_agent_supports_capability(int cap);

// Print Client API
int flexvdi_get_printer_list(GSList ** printer_list);
int flexvdi_share_printer(const char * printer);
int flexvdi_unshare_printer(const char * printer);

#endif /* _FLEXVDI_PORT_H_ */