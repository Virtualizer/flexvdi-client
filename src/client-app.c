#include <stdio.h>
#include <gtk/gtk.h>

#include "client-app.h"
#include "configuration.h"
#include "client-win.h"
#include "client-request.h"

struct _ClientApp {
    GtkApplication parent;
    ClientConf * conf;
    ClientAppWindow * main_window;
    ClientRequest * current_request;
    const gchar * username;
    const gchar * password;
    const gchar * desktop;
};

G_DEFINE_TYPE(ClientApp, client_app, GTK_TYPE_APPLICATION);

static void client_app_activate(GApplication * gapp);

static void client_app_class_init(ClientAppClass * class) {
    G_APPLICATION_CLASS(class)->activate = client_app_activate;
}

static gint client_app_handle_options(GApplication * gapp, GVariantDict * opts, gpointer u) {
    ClientApp * app = CLIENT_APP(gapp);
    if (client_conf_show_version(app->conf)) {
        printf("flexVDI Client v" VERSION_STRING "\n"
               "Copyright (C) 2018 Flexible Software Solutions S.L.\n");
        return 0;
    }

    return -1;
}

static void client_app_init(ClientApp * app) {
    app->conf = client_conf_new();
    app->username = app->password = app->desktop = "";
    g_application_add_main_option_entries(G_APPLICATION(app),
        client_conf_get_cmdline_entries(app->conf));
    g_signal_connect(app, "handle-local-options",
        G_CALLBACK(client_app_handle_options), NULL);
}

ClientApp * client_app_new(void) {
    return g_object_new(CLIENT_APP_TYPE,
                        "application-id", "com.flexvdi.client",
                        "flags", G_APPLICATION_NON_UNIQUE,
                        NULL);
}

static void config_button_pressed_handler(ClientAppWindow * win, gpointer user_data);
static gboolean key_event_handler(GtkWidget * widget, GdkEvent * event, gpointer user_data);
static void save_button_pressed_handler(ClientAppWindow * win, gpointer user_data);
static void login_button_pressed_handler(ClientAppWindow * win, gpointer user_data);

static void client_app_configure(ClientApp * app);
static void client_app_show_login(ClientApp * app);

static void client_app_activate(GApplication * gapp) {
    ClientApp * app = CLIENT_APP(gapp);
    app->main_window = client_app_window_new(app);
    gtk_window_present(GTK_WINDOW(app->main_window));

    const gchar * tid = client_conf_get_terminal_id(app->conf);
    g_autofree gchar * text = g_strconcat("Terminal ID: ", tid, NULL);
    client_app_window_set_info(app->main_window, text);

    client_app_window_set_config(app->main_window, app->conf);
    g_signal_connect(app->main_window, "config-button-pressed",
        G_CALLBACK(config_button_pressed_handler), app);
    g_signal_connect(app->main_window, "key-press-event",
        G_CALLBACK(key_event_handler), app);
    g_signal_connect(app->main_window, "save-button-pressed",
        G_CALLBACK(save_button_pressed_handler), app);
    g_signal_connect(app->main_window, "login-button-pressed",
        G_CALLBACK(login_button_pressed_handler), app);

    if (client_conf_get_host(app->conf) != NULL)
        client_app_show_login(app);
    else
        client_app_configure(app);
}

static void config_button_pressed_handler(ClientAppWindow * win, gpointer user_data) {
    client_app_configure(CLIENT_APP(user_data));
}

static gboolean key_event_handler(GtkWidget * widget, GdkEvent * event, gpointer user_data) {
    if (event->key.keyval == GDK_KEY_F3)
        client_app_configure(CLIENT_APP(user_data));
    return FALSE;
}

static void save_button_pressed_handler(ClientAppWindow * win, gpointer user_data) {
    client_app_show_login(CLIENT_APP(user_data));
}

static void client_app_request_desktop(ClientApp * app);

static void login_button_pressed_handler(ClientAppWindow * win, gpointer user_data) {
    ClientApp * app = CLIENT_APP(user_data);
    app->username = client_app_window_get_username(win);
    app->password = client_app_window_get_password(win);
    client_app_request_desktop(CLIENT_APP(user_data));
}

static void client_app_configure(ClientApp * app) {
    client_app_window_set_status(app->main_window, FALSE,
        "Please, provide the manager's address (and port, if it is not 443)");
    client_app_window_set_central_widget(app->main_window, "settings");
    client_app_window_set_central_widget_sensitive(app->main_window, TRUE);
    if (app->current_request) {
        client_request_cancel(app->current_request);
        g_clear_object(&app->current_request);
    }
}

static void authmode_request_cb(ClientRequest * req, gpointer user_data);

static void client_app_show_login(ClientApp * app) {
    client_app_window_set_status(app->main_window, FALSE,
        "Contacting server...");
    client_app_window_set_central_widget(app->main_window, "login");
    client_app_window_set_central_widget_sensitive(app->main_window, FALSE);
    app->username = app->password = app->desktop = "";
    g_clear_object(&app->current_request);
    g_autofree gchar * req_body = g_strdup_printf(
        "{\"hwaddress\": \"%s\"}", client_conf_get_terminal_id(app->conf));
    app->current_request = client_request_new_with_data(app->conf,
        "/vdi/authmode", req_body, authmode_request_cb, app);
}

static void authmode_request_cb(ClientRequest * req, gpointer user_data) {
    ClientApp * app = CLIENT_APP(user_data);
    g_autoptr(GError) error = NULL;
    JsonNode * root = client_request_get_result(req, &error);
    if (error) {
        client_app_window_set_status(app->main_window, TRUE,
            "Failed to contact server");
        g_warning("Request failed: %s", error->message);
    } else if (JSON_NODE_HOLDS_OBJECT(root)) {
        JsonObject * response = json_node_get_object(root);
        const gchar * status = json_object_get_string_member(response, "status");
        const gchar * auth_mode = json_object_get_string_member(response, "auth_mode");
        if (g_strcmp0(status, "OK") != 0) {
            client_app_window_set_status(app->main_window, TRUE,
                "Access denied");
        } else if (g_strcmp0(auth_mode, "active_directory") == 0) {
            client_app_window_set_status(app->main_window, FALSE,
                "Fill in your credentials");
            client_app_window_set_central_widget_sensitive(app->main_window, TRUE);
        } else {
            client_app_request_desktop(app);
        }
    } else {
        client_app_window_set_status(app->main_window, TRUE,
            "Invalid response from server");
        g_warning("Invalid response from server, see debug messages");
    }
}

static void desktop_request_cb(ClientRequest * req, gpointer user_data);

static void client_app_request_desktop(ClientApp * app) {
    client_app_window_set_status(app->main_window, FALSE,
        "Requesting desktop policy...");
    client_app_window_set_central_widget_sensitive(app->main_window, FALSE);
    g_clear_object(&app->current_request);
    g_autofree gchar * req_body = g_strdup_printf(
        "{\"hwaddress\": \"%s\", \"username\": \"%s\", \"password\": \"%s\", \"desktop\": \"%s\"}",
        client_conf_get_terminal_id(app->conf),
        app->username, app->password, app->desktop);
    app->current_request = client_request_new_with_data(app->conf,
        "/vdi/desktop", req_body, desktop_request_cb, app);
}

static gboolean client_app_repeat_request_desktop(gpointer user_data) {
    client_app_request_desktop(CLIENT_APP(user_data));
    return FALSE; // Cancel timeout
}

static void desktop_request_cb(ClientRequest * req, gpointer user_data) {
    ClientApp * app = CLIENT_APP(user_data);
    g_autoptr(GError) error = NULL;
    gboolean invalid = FALSE;
    JsonNode * root = client_request_get_result(req, &error);
    if (error) {
        client_app_window_set_status(app->main_window, TRUE,
            "Failed to contact server");
        g_warning("Request failed: %s", error->message);
    } else if (JSON_NODE_HOLDS_OBJECT(root)) {
        JsonObject * response = json_node_get_object(root);
        const gchar * status = json_object_get_string_member(response, "status");
        const gchar * message = json_object_get_string_member(response, "message");
        if (g_strcmp0(status, "OK") == 0) {
            // Manage desktop
        } else if (g_strcmp0(status, "Pending") == 0) {
            client_app_window_set_status(app->main_window, FALSE,
                "Preparing desktop...");
            g_timeout_add_seconds(3, client_app_repeat_request_desktop, app);
        } else if (g_strcmp0(status, "Error") == 0) {
            client_app_window_set_status(app->main_window, TRUE, message);
            client_app_window_set_central_widget_sensitive(app->main_window, TRUE);
        } else if (g_strcmp0(status, "SelectDesktop") == 0) {
            JsonObject * desktops = json_object_get_object_member(response, "message");
            if (desktops) {
                // Show desktops
            } else invalid = TRUE;
        } else invalid = TRUE;
    } else invalid = TRUE;

    if (invalid) {
        client_app_window_set_status(app->main_window, TRUE,
            "Invalid response from server");
        g_warning("Invalid response from server, see debug messages");
    }
}
