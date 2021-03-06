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

#ifndef _CLIENT_APP_H
#define _CLIENT_APP_H

#include <gtk/gtk.h>


/*
 * ClientApp
 * 
 * Main application class. It derives from GtkApplication and controls
 * the logic of the application.
 */
#define CLIENT_APP_TYPE (client_app_get_type())
G_DECLARE_FINAL_TYPE(ClientApp, client_app, CLIENT, APP, GtkApplication)

ClientApp * client_app_new(void);


#endif /* _CLIENT_APP_H */
