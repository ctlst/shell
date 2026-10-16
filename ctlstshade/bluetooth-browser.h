/* BlueZ's object cache supplies names asynchronously; never block GTK on a
 * bluetoothctl snapshot. Discovery is owned only while this page is visible. */
static gboolean
bt_bool(GDBusProxy *proxy, const char *key)
{
	g_autoptr(GVariant) value = g_dbus_proxy_get_cached_property(proxy, key);
	return value != NULL && g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)
	    && g_variant_get_boolean(value);
}

static char *
bt_string(GDBusProxy *proxy, const char *key)
{
	g_autoptr(GVariant) value = g_dbus_proxy_get_cached_property(proxy, key);
	return value != NULL && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) ?
	    g_variant_dup_string(value, NULL) : NULL;
}

static bool
bt_page_visible(struct app *app)
{
	return gtk_widget_get_visible(GTK_WIDGET(app->window)) &&
	    g_strcmp0(gtk_stack_get_visible_child_name(app->stack), "bluetooth") == 0;
}

static void bt_reconcile(struct app *app);

static void
bt_scan_done(GObject *source, GAsyncResult *result, gpointer data)
{
	struct app *app = data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) reply = g_dbus_proxy_call_finish(
	    G_DBUS_PROXY(source), result, &error);
	app->bt_busy = false;
	if (reply != NULL) {
		app->bt_scanning = app->bt_starting;
		if (app->bt_scanning && bt_page_visible(app) && !app->bt_status_action)
			gtk_label_set_text(app->bluetooth_status, "Scanning nearby devices…");
		if (!app->bt_scanning)
			g_clear_object(&app->bt_adapter);
		bt_reconcile(app); /* Page may have closed while StartDiscovery ran. */
	} else {
		app->bt_scanning = false;
		g_clear_object(&app->bt_adapter);
		if (bt_page_visible(app)) {
			g_autofree char *message = g_strdup_printf(
			    "Bluetooth scan failed: %s. Tap SCAN to retry.", error->message);
			gtk_label_set_text(app->bluetooth_status, message);
		}
	}
}

static void
bt_reconcile(struct app *app)
{
	bool wanted = bt_page_visible(app);
	if (app->bt_manager == NULL || app->bt_busy)
		return;
	if (!wanted && app->bt_scanning) {
		app->bt_busy = true;
		app->bt_starting = false;
		g_dbus_proxy_call(app->bt_adapter, "StopDiscovery", NULL,
		    G_DBUS_CALL_FLAGS_NONE, 5000, NULL, bt_scan_done, app);
	} else if (wanted && !app->bt_scanning) {
		GList *objects = g_dbus_object_manager_get_objects(app->bt_manager);
		for (GList *item = objects; item != NULL; item = item->next) {
			g_autoptr(GDBusInterface) adapter = g_dbus_object_get_interface(
			    item->data, "org.bluez.Adapter1");
			if (adapter == NULL || !bt_bool(G_DBUS_PROXY(adapter), "Powered"))
				continue;
			app->bt_adapter = g_object_ref(G_DBUS_PROXY(adapter));
			app->bt_busy = true;
			app->bt_starting = true;
			if (!app->bt_status_action)
				gtk_label_set_text(app->bluetooth_status, "Scanning nearby devices…");
			g_dbus_proxy_call(app->bt_adapter, "StartDiscovery", NULL,
			    G_DBUS_CALL_FLAGS_NONE, 5000, NULL, bt_scan_done, app);
			break;
		}
		g_list_free_full(objects, g_object_unref);
		if (!app->bt_busy)
			gtk_label_set_text(app->bluetooth_status,
			    "No powered Bluetooth adapter. Turn Bluetooth on to scan.");
	}
}

static void
bt_visibility_changed(GObject *object, GParamSpec *property, gpointer data)
{
	(void)object;
	(void)property;
	struct app *app = data;
	if (app->bt_pair_active && !app->bt_pair_updating_dialog &&
	    (!gtk_widget_get_visible(GTK_WIDGET(app->window)) ||
	    g_strcmp0(gtk_stack_get_visible_child_name(app->stack), "dialog") != 0))
		bt_pair_cancel(app);
	if (bt_page_visible(app))
		refresh_bluetooth_devices(app);
	bt_reconcile(app);
}

static void
bt_update_row(struct app *app, GDBusProxy *device, GHashTable *seen)
{
	const char *path = g_dbus_proxy_get_object_path(device);
	g_autofree char *address = bt_string(device, "Address");
	g_autofree char *name = bt_string(device, "Alias");
	g_autofree char *remote_name = bt_string(device, "Name");
	g_autofree char *fallback = address != NULL ? g_strdup(address) : NULL;
	if (address == NULL)
		return;
	for (char *c = fallback; *c != '\0'; c++)
		if (*c == ':') *c = '-';
	if (name == NULL || *name == '\0' || g_strcmp0(name, address) == 0 ||
	    g_ascii_strcasecmp(name, fallback) == 0) {
		g_free(name);
		name = g_strdup(remote_name != NULL && *remote_name != '\0' ?
		    remote_name : "Unknown device");
	}
	bool paired = bt_bool(device, "Paired");
	bool connected = bt_bool(device, "Connected");
	const char *state = connected ? "Connected — tap to disconnect" :
	    (paired ? "Paired — tap to connect" : "Available — tap to pair");
	g_autofree char *label = g_strdup_printf("%s\n%s\n%s", name, address, state);
	GtkWidget *button = g_hash_table_lookup(app->bt_rows, path);
	if (button == NULL) {
		button = gtk_button_new();
		gtk_widget_add_css_class(button, "device-row");
		g_signal_connect(button, "clicked", G_CALLBACK(bluetooth_device_clicked), app);
		gtk_box_append(app->bluetooth_list, button);
		g_hash_table_insert(app->bt_rows, g_strdup(path), button);
	}
	/* Keep the button itself stable: delayed names must not steal focus or
	 * replace the touch target underneath a finger. */
	if (g_strcmp0(gtk_button_get_label(GTK_BUTTON(button)), label) != 0)
		gtk_button_set_label(GTK_BUTTON(button), label);
	GtkWidget *child = gtk_button_get_child(GTK_BUTTON(button));
	gtk_label_set_xalign(GTK_LABEL(child), 0);
	gtk_label_set_wrap(GTK_LABEL(child), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(child), PANGO_WRAP_WORD_CHAR);
	if (connected)
		gtk_widget_add_css_class(button, "device-connected");
	else
		gtk_widget_remove_css_class(button, "device-connected");
	g_object_set_data_full(G_OBJECT(button), "address", g_strdup(address), g_free);
	g_object_set_data_full(G_OBJECT(button), "device-name", g_strdup(name), g_free);
	g_object_set_data(G_OBJECT(button), "paired", GINT_TO_POINTER(paired));
	g_object_set_data(G_OBJECT(button), "connected", GINT_TO_POINTER(connected));
	g_hash_table_add(seen, g_strdup(path));
}

static gboolean
bt_refresh_idle(gpointer data)
{
	struct app *app = data;
	app->bt_refresh_source = 0;
	if (bt_page_visible(app))
		refresh_bluetooth_devices(app);
	return G_SOURCE_REMOVE;
}

static void
bt_queue_refresh(struct app *app)
{
	if (bt_page_visible(app) && app->bt_refresh_source == 0)
		app->bt_refresh_source = g_timeout_add(100, bt_refresh_idle, app);
}

static void
bt_object_changed(GDBusObjectManager *manager, GDBusObject *object, gpointer data)
{
	(void)manager;
	struct app *app = data;
	if (app->bt_adapter != NULL && !app->bt_busy &&
	    g_strcmp0(g_dbus_object_get_object_path(object),
	    g_dbus_proxy_get_object_path(app->bt_adapter)) == 0) {
		g_autoptr(GDBusObject) current = g_dbus_object_manager_get_object(
		    app->bt_manager, g_dbus_object_get_object_path(object));
		if (current == NULL) {
			app->bt_scanning = false;
			g_clear_object(&app->bt_adapter);
		}
	}
	bt_queue_refresh(app);
	bt_reconcile(app);
}

static void
bt_properties_changed(GDBusObjectManagerClient *manager,
	GDBusObjectProxy *object, GDBusProxy *interface, GVariant *changed,
	const char *const *invalidated, gpointer data)
{
	(void)manager;
	(void)object;
	(void)changed;
	(void)invalidated;
	struct app *app = data;
	if (g_strcmp0(g_dbus_proxy_get_interface_name(interface), "org.bluez.Adapter1") == 0) {
		if (!bt_bool(interface, "Powered") && !app->bt_busy) {
			app->bt_scanning = false;
			g_clear_object(&app->bt_adapter);
		}
		bt_reconcile(app);
	}
	bt_queue_refresh(app);
}

static void
bt_owner_changed(GObject *object, GParamSpec *property, gpointer data)
{
	(void)property;
	struct app *app = data;
	g_autofree char *owner = g_dbus_object_manager_client_get_name_owner(
	    G_DBUS_OBJECT_MANAGER_CLIENT(object));
	if (owner == NULL) {
		app->bt_agent_registered = false;
		bt_pair_cancel(app);
	}
}

static void
bt_manager_ready(GObject *source, GAsyncResult *result, gpointer data)
{
	(void)source;
	struct app *app = data;
	g_autoptr(GError) error = NULL;
	app->bt_loading = false;
	app->bt_manager = g_dbus_object_manager_client_new_for_bus_finish(result, &error);
	if (app->bt_manager == NULL) {
		gtk_label_set_text(app->bluetooth_status, "Bluetooth service unavailable. Tap SCAN to retry.");
		return;
	}
	g_signal_connect(app->bt_manager, "object-added", G_CALLBACK(bt_object_changed), app);
	g_signal_connect(app->bt_manager, "notify::name-owner", G_CALLBACK(bt_owner_changed), app);
	g_signal_connect(app->bt_manager, "object-removed", G_CALLBACK(bt_object_changed), app);
	g_signal_connect(app->bt_manager, "interface-proxy-properties-changed",
	    G_CALLBACK(bt_properties_changed), app);
	bt_queue_refresh(app);
	bt_reconcile(app);
}

static void
refresh_bluetooth_devices(struct app *app)
{
	if (app->bt_rows == NULL)
		app->bt_rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	if (app->bt_manager == NULL) {
		if (!app->bt_loading) {
			app->bt_loading = true;
			gtk_label_set_text(app->bluetooth_status, "Loading Bluetooth devices…");
			g_dbus_object_manager_client_new_for_bus(G_BUS_TYPE_SYSTEM,
			    G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
			    "org.bluez", "/", NULL, NULL, NULL, NULL, bt_manager_ready, app);
		}
		return;
	}
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GList *objects = g_dbus_object_manager_get_objects(app->bt_manager);
	for (GList *item = objects; item != NULL; item = item->next) {
		g_autoptr(GDBusInterface) device = g_dbus_object_get_interface(item->data, "org.bluez.Device1");
		if (device != NULL)
			bt_update_row(app, G_DBUS_PROXY(device), seen);
	}
	g_list_free_full(objects, g_object_unref);
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, app->bt_rows);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if (!g_hash_table_contains(seen, key)) {
			gtk_box_remove(app->bluetooth_list, value);
			g_hash_table_iter_remove(&iter);
		}
	}
}
