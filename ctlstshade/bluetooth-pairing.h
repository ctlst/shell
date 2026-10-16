/* An application-local agent, never the system default. Only the selected
 * device and BlueZ's current bus owner may request authentication. */
#define BT_AGENT_PATH "/dev/ctlst/Shade/Pairing"

static GDBusConnection *
bt_connection(struct app *app)
{
	return g_dbus_object_manager_client_get_connection(G_DBUS_OBJECT_MANAGER_CLIENT(app->bt_manager));
}

static void
bt_pair_reply(struct app *app, GVariant *value)
{
	if (app->bt_pair_request == NULL) return;
	if (value != NULL)
		g_dbus_method_invocation_return_value(app->bt_pair_request, value);
	else
		g_dbus_method_invocation_return_dbus_error(app->bt_pair_request,
		    "org.bluez.Error.Canceled", "Pairing canceled");
	g_clear_object(&app->bt_pair_request);
}

static void
bt_pair_finish(struct app *app, const char *message)
{
	app->bt_pair_active = false;
	app->bt_status_action = true;
	app->bt_pair_call_pending = false;
	if (app->bt_pair_timeout != 0) {
		g_source_remove(app->bt_pair_timeout);
		app->bt_pair_timeout = 0;
	}
	bt_pair_reply(app, NULL);
	app->bt_pair_entry = NULL;
	end_text_input(app);
	/* Never bring back a page the user has already dismissed. */
	if (g_strcmp0(gtk_stack_get_visible_child_name(app->stack), "dialog") == 0)
		gtk_stack_set_visible_child_name(app->stack, "bluetooth");
	gtk_label_set_text(app->bluetooth_status, message);
}

static void
bt_pair_cancel(struct app *app)
{
	if (!app->bt_pair_active) return;
	/* BlueZ CancelPairing outside a pending Pair may remove an existing bond. */
	if (app->bt_pair_call_pending)
		g_dbus_connection_call(bt_connection(app), "org.bluez", app->bt_pair_path,
		    "org.bluez.Device1", "CancelPairing", NULL, NULL,
		    G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);
	bt_pair_finish(app, "Pairing canceled");
}

static gboolean
bt_pair_expired(gpointer data)
{
	struct app *app = data;
	app->bt_pair_timeout = 0;
	bt_pair_cancel(app);
	gtk_label_set_text(app->bluetooth_status, "Pairing timed out. Put the device in pairing mode and retry.");
	return G_SOURCE_REMOVE;
}

static void
bt_pair_cancel_clicked(GtkButton *button, gpointer data)
{
	(void)button;
	bt_pair_cancel(data);
}

static bool
bt_valid_pin(const char *text, bool numeric)
{
	size_t length = strlen(text);
	if (length == 0 || length > (numeric ? 6U : 16U)) return false;
	for (const unsigned char *p = (const unsigned char *)text; *p; p++)
		if (numeric ? !g_ascii_isdigit(*p) : (*p < 32 || *p > 126)) return false;
	return true;
}

static void
bt_pair_accept(GtkButton *button, gpointer data)
{
	(void)button;
	struct app *app = data;
	if (!app->bt_pair_active || app->bt_pair_request == NULL) return;
	if (app->bt_pair_entry != NULL) {
		const char *text = gtk_editable_get_text(GTK_EDITABLE(app->bt_pair_entry));
		if (!bt_valid_pin(text, app->bt_pair_numeric)) {
			gtk_widget_add_css_class(GTK_WIDGET(app->bt_pair_entry), "error");
			return;
		}
		bt_pair_reply(app, app->bt_pair_numeric ?
		    g_variant_new("(u)", (guint32)g_ascii_strtoull(text, NULL, 10)) :
		    g_variant_new("(s)", text));
	} else {
		bt_pair_reply(app, g_variant_new("()"));
	}
	app->bt_pair_entry = NULL;
	end_text_input(app);
}

static void
bt_pair_prompt(struct app *app, const char *instructions, bool confirm,
    bool entry, bool numeric)
{
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	GtkWidget *title = gtk_label_new(app->bt_pair_name);
	GtkWidget *detail = gtk_label_new(instructions);
	GtkWidget *cancel = make_button("Cancel pairing", "shade-action",
	    G_CALLBACK(bt_pair_cancel_clicked), app);
	gtk_widget_add_css_class(root, "password-window");
	gtk_widget_add_css_class(title, "shade-title");
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	gtk_label_set_xalign(GTK_LABEL(detail), 0);
	gtk_box_append(GTK_BOX(root), title);
	gtk_box_append(GTK_BOX(root), detail);
	app->bt_pair_entry = NULL;
	app->bt_pair_numeric = numeric;
	if (entry) {
		app->bt_pair_entry = GTK_ENTRY(gtk_entry_new());
		gtk_entry_set_max_length(app->bt_pair_entry, numeric ? 6 : 16);
		gtk_entry_set_input_purpose(app->bt_pair_entry,
		    numeric ? GTK_INPUT_PURPOSE_DIGITS : GTK_INPUT_PURPOSE_FREE_FORM);
		gtk_box_append(GTK_BOX(root), GTK_WIDGET(app->bt_pair_entry));
	}
	if (confirm || entry) {
		GtkWidget *accept = make_button(entry ? "Submit code" : "Confirm",
		    "shade-action", G_CALLBACK(bt_pair_accept), app);
		gtk_box_append(GTK_BOX(root), accept);
	}
	gtk_box_append(GTK_BOX(root), cancel);
	app->bt_pair_updating_dialog = true;
	show_dialog_page(app, root);
	app->bt_pair_updating_dialog = false;
	if (entry) begin_text_input(app, GTK_WIDGET(app->bt_pair_entry));
}

static const char *bt_agent_xml =
    "<node><interface name='org.bluez.Agent1'>"
    "<method name='Release'/>"
    "<method name='RequestPinCode'><arg type='o' direction='in'/><arg type='s' direction='out'/></method>"
    "<method name='DisplayPinCode'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
    "<method name='RequestPasskey'><arg type='o' direction='in'/><arg type='u' direction='out'/></method>"
    "<method name='DisplayPasskey'><arg type='o' direction='in'/><arg type='u' direction='in'/><arg type='q' direction='in'/></method>"
    "<method name='RequestConfirmation'><arg type='o' direction='in'/><arg type='u' direction='in'/></method>"
    "<method name='RequestAuthorization'><arg type='o' direction='in'/></method>"
    "<method name='AuthorizeService'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
    "<method name='Cancel'/></interface></node>";

static void
bt_agent_method(GDBusConnection *connection, const char *sender, const char *path,
    const char *interface, const char *method, GVariant *args,
    GDBusMethodInvocation *call, gpointer data)
{
	(void)connection; (void)path; (void)interface;
	struct app *app = data;
	g_autofree char *owner = g_dbus_object_manager_client_get_name_owner(
	    G_DBUS_OBJECT_MANAGER_CLIENT(app->bt_manager));
	if (owner == NULL || g_strcmp0(sender, owner) != 0) goto rejected;
	if (strcmp(method, "Release") == 0) {
		app->bt_agent_registered = false;
		g_dbus_method_invocation_return_value(call, NULL);
		bt_pair_cancel(app);
		return;
	}
	if (strcmp(method, "Cancel") == 0) {
		bt_pair_reply(app, NULL);
		g_dbus_method_invocation_return_value(call, NULL);
		return; /* Pair completion decides success/failure, not display dismissal. */
	}
	const char *device;
	g_variant_get_child(args, 0, "&o", &device);
	if (!app->bt_pair_active || !gtk_widget_get_visible(GTK_WIDGET(app->window)) ||
	    g_strcmp0(device, app->bt_pair_path) != 0) goto rejected;
	if (strcmp(method, "DisplayPinCode") == 0 || strcmp(method, "DisplayPasskey") == 0) {
		g_autofree char *prompt = NULL;
		if (strcmp(method, "DisplayPinCode") == 0) {
			const char *pin;
			g_variant_get_child(args, 1, "&s", &pin);
			prompt = g_strdup_printf("Type %s on the Bluetooth keyboard, then press Enter.", pin);
		} else {
			guint32 pin; guint16 entered;
			g_variant_get_child(args, 1, "u", &pin);
			g_variant_get_child(args, 2, "q", &entered);
			prompt = g_strdup_printf("Type %06u on the Bluetooth keyboard, then press Enter.\n%u digits entered.", pin, entered);
		}
		bt_pair_prompt(app, prompt, false, false, false);
		g_dbus_method_invocation_return_value(call, NULL);
		return;
	}
	if (app->bt_pair_request != NULL) goto rejected;
	if (strcmp(method, "RequestPinCode") == 0 && app->bt_pair_keyboard) {
		/* Kernel CSPRNG, not a predictable clock/GLib PRNG PIN. */
		guint32 random;
		if (getrandom(&random, sizeof(random), 0) != sizeof(random)) goto rejected;
		g_autofree char *pin = g_strdup_printf("%06u", random % 1000000);
		g_autofree char *prompt = g_strdup_printf(
		    "Type %s on the Bluetooth keyboard, then press Enter.", pin);
		bt_pair_prompt(app, prompt, false, false, false);
		g_dbus_method_invocation_return_value(call, g_variant_new("(s)", pin));
		return;
	}
	app->bt_pair_request = g_object_ref(call);
	if (strcmp(method, "RequestPinCode") == 0 || strcmp(method, "RequestPasskey") == 0) {
		bt_pair_prompt(app, "Enter the code shown on the other device or specified in its pairing instructions.",
		    false, true, strcmp(method, "RequestPasskey") == 0);
	} else if (strcmp(method, "RequestConfirmation") == 0) {
		guint32 pin;
		g_variant_get_child(args, 1, "u", &pin);
		g_autofree char *prompt = g_strdup_printf("Does the other device show %06u? Confirm only if the codes match.", pin);
		bt_pair_prompt(app, prompt, true, false, false);
	} else {
		bt_pair_prompt(app, "Allow this selected device to pair and use its Bluetooth services?", true, false, false);
	}
	return;
rejected:
	g_dbus_method_invocation_return_dbus_error(call, "org.bluez.Error.Rejected", "No matching user-initiated pairing");
}

static void bt_pair_call(struct app *app);
static void bt_pair_verify(struct app *app);

static bool
bt_pair_result(struct app *app, GObject *source, GAsyncResult *result, GVariant **reply)
{
	g_autoptr(GError) error = NULL;
	*reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
	app->bt_pair_busy = false;
	if (!app->bt_pair_active) return false;
	if (*reply == NULL) {
		g_autofree char *message = g_strdup_printf("Pairing failed: %s", error->message);
		bt_pair_finish(app, message);
		return false;
	}
	return true;
}

static void
bt_pair_connected(GObject *source, GAsyncResult *result, gpointer data)
{
	struct app *app = data;
	g_autoptr(GVariant) reply = NULL;
	if (bt_pair_result(app, source, result, &reply))
		bt_pair_finish(app, app->bt_pair_keyboard ?
		    "Keyboard bonded and input profile connected. Try typing in an app." :
		    "Device bonded and connected.");
}

static void
bt_pair_trusted(GObject *source, GAsyncResult *result, gpointer data)
{
	struct app *app = data;
	g_autoptr(GVariant) reply = NULL;
	if (!bt_pair_result(app, source, result, &reply)) return;
	app->bt_pair_busy = true;
	g_dbus_connection_call(bt_connection(app), "org.bluez", app->bt_pair_path,
	    "org.bluez.Device1", app->bt_pair_classic ? "ConnectProfile" : "Connect",
	    app->bt_pair_classic ? g_variant_new("(s)", "00001124-0000-1000-8000-00805f9b34fb") : NULL,
	    NULL, G_DBUS_CALL_FLAGS_NONE, 20000, NULL, bt_pair_connected, app);
}

static void
bt_pair_verified(GObject *source, GAsyncResult *result, gpointer data)
{
	struct app *app = data;
	g_autoptr(GVariant) reply = NULL;
	if (!bt_pair_result(app, source, result, &reply)) return;
	g_autoptr(GVariant) props = g_variant_get_child_value(reply, 0);
	gboolean bonded = FALSE;
	if (!g_variant_lookup(props, "Bonded", "b", &bonded) || !bonded) {
		bt_pair_finish(app, "No saved Bluetooth bond. Put the device in pairing mode and retry; input remains protected.");
		return;
	}
	app->bt_pair_busy = true;
	g_dbus_connection_call(bt_connection(app), "org.bluez", app->bt_pair_path,
	    "org.freedesktop.DBus.Properties", "Set",
	    g_variant_new("(ssv)", "org.bluez.Device1", "Trusted", g_variant_new_boolean(TRUE)),
	    NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, bt_pair_trusted, app);
}

static void
bt_pair_verify(struct app *app)
{
	app->bt_pair_busy = true;
	g_dbus_connection_call(bt_connection(app), "org.bluez", app->bt_pair_path,
	    "org.freedesktop.DBus.Properties", "GetAll", g_variant_new("(s)", "org.bluez.Device1"),
	    NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, bt_pair_verified, app);
}

static void
bt_pair_paired(GObject *source, GAsyncResult *result, gpointer data)
{
	struct app *app = data;
	g_autoptr(GVariant) reply = NULL;
	app->bt_pair_call_pending = false;
	if (bt_pair_result(app, source, result, &reply)) bt_pair_verify(app);
}

static void
bt_pair_call(struct app *app)
{
	if (!app->bt_pair_active) return;
	app->bt_pair_busy = true;
	app->bt_pair_call_pending = true;
	g_dbus_connection_call(bt_connection(app), "org.bluez", app->bt_pair_path,
	    "org.bluez.Device1", "Pair", NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
	    120000, NULL, bt_pair_paired, app);
}

static void
bt_agent_ready(GObject *source, GAsyncResult *result, gpointer data)
{
	struct app *app = data;
	g_autoptr(GVariant) reply = NULL;
	bool proceed = bt_pair_result(app, source, result, &reply);
	app->bt_agent_registered = reply != NULL;
	if (proceed) bt_pair_call(app);
}

static void
bt_pair_start(struct app *app, const char *address, const char *name)
{
	if (app->bt_pair_active || app->bt_pair_busy) return;
	app->bt_status_action = false;
	if (app->bt_manager == NULL) return;
	GList *objects = g_dbus_object_manager_get_objects(app->bt_manager);
	g_autofree char *target = NULL;
	for (GList *item = objects; item != NULL; item = item->next) {
		g_autoptr(GDBusInterface) device = g_dbus_object_get_interface(item->data, "org.bluez.Device1");
		if (device == NULL) continue;
		g_autofree char *candidate = bt_string(G_DBUS_PROXY(device), "Address");
		if (g_strcmp0(candidate, address) != 0) continue;
		target = g_strdup(g_dbus_object_get_object_path(item->data));
		g_autofree char *icon = bt_string(G_DBUS_PROXY(device), "Icon");
		app->bt_pair_keyboard = g_strcmp0(icon, "input-keyboard") == 0;
		app->bt_pair_classic = false;
		g_autoptr(GVariant) uuids = g_dbus_proxy_get_cached_property(G_DBUS_PROXY(device), "UUIDs");
		if (app->bt_pair_keyboard && uuids != NULL) {
			GVariantIter iter;
			const char *uuid;
			g_variant_iter_init(&iter, uuids);
			while (g_variant_iter_next(&iter, "&s", &uuid))
				if (g_strcmp0(uuid, "00001124-0000-1000-8000-00805f9b34fb") == 0)
					app->bt_pair_classic = true;
		}
		break;
	}
	g_list_free_full(objects, g_object_unref);
	if (target == NULL) return;
	g_free(app->bt_pair_path); g_free(app->bt_pair_name);
	app->bt_pair_path = g_steal_pointer(&target);
	app->bt_pair_name = g_strdup(name);
	/* Show the dialog before marking it active (navigation cancels pairing). */
	bt_pair_prompt(app, "Keep the device in pairing mode. A code or confirmation will appear here if required.", false, false, false);
	app->bt_pair_active = true;
	app->bt_pair_timeout = g_timeout_add_seconds(120, bt_pair_expired, app);
	if (app->bt_agent_registered) { bt_pair_call(app); return; }
	if (app->bt_agent_id == 0) {
		g_autoptr(GError) error = NULL;
		g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(bt_agent_xml, &error);
		static const GDBusInterfaceVTable vtable = {.method_call = bt_agent_method};
		app->bt_agent_id = g_dbus_connection_register_object(bt_connection(app),
		    BT_AGENT_PATH, info->interfaces[0], &vtable, app, NULL, &error);
		if (app->bt_agent_id == 0) { bt_pair_finish(app, error->message); return; }
	}
	app->bt_pair_busy = true;
	g_dbus_connection_call(bt_connection(app), "org.bluez", "/org/bluez",
	    "org.bluez.AgentManager1", "RegisterAgent", g_variant_new("(os)", BT_AGENT_PATH, "KeyboardDisplay"),
	    NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, bt_agent_ready, app);
}
