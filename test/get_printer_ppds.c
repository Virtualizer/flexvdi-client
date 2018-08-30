/**
 * Copyright Flexible Software Solutions S.L. 2014
 **/

#include <stdio.h>
#include <glib.h>
#include "src/printclient.h"
#include "src/flexvdi-port.h"


int main(int argc, char * argv[]) {
    GSList * printers, * i;
    flexvdi_get_printer_list(&printers);
    for (i = printers; i != NULL; i = g_slist_next(i)) {
        char * fileName = get_ppd_file((const char *)i->data);
        printf("Printer %s: %s\n", (const char *)i->data, fileName);
        g_free(fileName);
    }
    g_slist_free_full(printers, g_free);
    return 0;
}
