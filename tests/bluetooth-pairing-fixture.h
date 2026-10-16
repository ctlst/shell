/* Private-bus pairing fixture shared by Pixel and portable native tests. */
static const char *pair_target = "/org/bluez/hci0/dev_00_00_00_00_00_17";
static GDBusMethodInvocation *pair_pending;
static char *pair_agent;
static bool fixture_bonded, pin_received, agent_rejected;
static unsigned fixture_trust, fixture_connect, fixture_cancel;
static void spin(void);

static void
agent_pin_reply(GObject *source, GAsyncResult *result, gpointer data)
{
	(void)data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
	g_assert_no_error(error);
	const char *pin;
	g_variant_get(reply, "(&s)", &pin);
	g_assert_cmpuint(strlen(pin), ==, 6);
	g_assert_true(bt_valid_pin(pin, true));
	pin_received = true;
}

static void
agent_reject_reply(GObject *source, GAsyncResult *result, gpointer data)
{
	(void)data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
	g_assert_null(reply);
	g_autofree char *remote = g_dbus_error_get_remote_error(error);
	g_assert_cmpstr(remote, ==, "org.bluez.Error.Rejected");
	agent_rejected = true;
}

static bool
pairing_method(GDBusConnection *connection, const char *sender,
    const char *name, GVariant *args, GDBusMethodInvocation *call)
{
	if (strcmp(name, "RegisterAgent") == 0) {
		const char *path, *capability;
		g_variant_get(args, "(&o&s)", &path, &capability);
		g_assert_cmpstr(path, ==, BT_AGENT_PATH);
		g_assert_cmpstr(capability, ==, "KeyboardDisplay");
		g_free(pair_agent); pair_agent = g_strdup(sender);
		g_dbus_method_invocation_return_value(call, NULL);
	} else if (strcmp(name, "Pair") == 0) {
		g_assert_null(pair_pending);
		pair_pending = g_object_ref(call);
		pin_received = false;
		g_dbus_connection_call(connection, sender, BT_AGENT_PATH,
		    "org.bluez.Agent1", "RequestPinCode", g_variant_new("(o)", pair_target),
		    NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, agent_pin_reply, NULL);
	} else if (strcmp(name, "CancelPairing") == 0) {
		fixture_cancel++;
		g_assert_nonnull(pair_pending);
		g_dbus_method_invocation_return_dbus_error(pair_pending,
		    "org.bluez.Error.AuthenticationCanceled", "Canceled by user");
		g_clear_object(&pair_pending);
		g_dbus_method_invocation_return_value(call, NULL);
	} else if (strcmp(name, "ConnectProfile") == 0) {
		g_assert_true(fixture_bonded);
		const char *uuid;
		g_variant_get(args, "(&s)", &uuid);
		g_assert_cmpstr(uuid, ==, "00001124-0000-1000-8000-00805f9b34fb");
		fixture_connect++;
		g_dbus_method_invocation_return_value(call, NULL);
	} else return false;
	return true;
}

static GVariant *
pair_property(GDBusConnection *connection, const char *sender, const char *path,
    const char *interface, const char *property, GError **error, gpointer data)
{
	(void)connection; (void)sender; (void)path; (void)interface; (void)error; (void)data;
	return g_variant_new_boolean(strcmp(property, "Bonded") == 0 ? fixture_bonded : FALSE);
}

static gboolean
pair_set_property(GDBusConnection *connection, const char *sender, const char *path,
    const char *interface, const char *property, GVariant *value, GError **error, gpointer data)
{
	(void)connection; (void)sender; (void)path; (void)interface; (void)error; (void)data;
	g_assert_cmpstr(property, ==, "Trusted");
	g_assert_true(g_variant_get_boolean(value));
	g_assert_true(fixture_bonded);
	fixture_trust++;
	return TRUE;
}

static void
pair_method_callback(GDBusConnection *connection, const char *sender, const char *path,
    const char *interface, const char *name, GVariant *args,
    GDBusMethodInvocation *call, gpointer data)
{
	(void)path; (void)interface; (void)data;
	g_assert_true(pairing_method(connection, sender, name, args, call));
}

static void
pairing_register(GDBusConnection *service)
{
	const char *xml = "<node><interface name='org.bluez.AgentManager1'>"
	    "<method name='RegisterAgent'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
	    "</interface><interface name='org.bluez.Device1'>"
	    "<method name='Pair'/><method name='CancelPairing'/>"
	    "<method name='ConnectProfile'><arg type='s' direction='in'/></method>"
	    "<property name='Bonded' type='b' access='read'/>"
	    "<property name='Trusted' type='b' access='readwrite'/>"
	    "</interface></node>";
	g_autoptr(GError) error = NULL;
	g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(xml, &error);
	const GDBusInterfaceVTable vtable = {.method_call = pair_method_callback,
	    .get_property = pair_property, .set_property = pair_set_property};
	g_dbus_connection_register_object(service, "/org/bluez", info->interfaces[0], &vtable, NULL, NULL, &error);
	g_assert_no_error(error);
	g_dbus_connection_register_object(service, pair_target, info->interfaces[1], &vtable, NULL, NULL, &error);
	g_assert_no_error(error);
}

static void
assert_narrow(GtkWidget *widget)
{
	int minimum, natural;
	gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1, &minimum, &natural, NULL, NULL);
	g_assert_cmpint(minimum, <=, 320);
}

static bool
has_label(GtkWidget *widget, const char *text)
{
	if (GTK_IS_LABEL(widget) && strstr(gtk_label_get_text(GTK_LABEL(widget)), text)) return true;
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		if (has_label(child, text)) return true;
	return false;
}

static void
pairing_tests(struct app *app, GDBusConnection *service)
{
	const char *name = "Targus Multi-Device Compact Bluetooth Keyboard";
	gtk_stack_set_visible_child_name(app->stack, "bluetooth");
	gtk_window_present(app->window); spin();
	show_bluetooth_pair_confirmation(app, "00:00:00:00:00:17", name);
	assert_narrow(GTK_WIDGET(app->stack));
	gtk_stack_set_visible_child_name(app->stack, "settings");
	assert_narrow(GTK_WIDGET(app->stack)); /* Hidden long dialog cannot stretch Settings. */
	gtk_stack_set_visible_child_name(app->stack, "bluetooth");
	bt_pair_start(app, "00:00:00:00:00:17", name); spin();
	g_assert_true(pin_received);
	g_assert_true(app->bt_pair_active);
	g_assert_true(has_label(GTK_WIDGET(app->stack), "then press Enter"));
	assert_narrow(GTK_WIDGET(app->stack));
	agent_rejected = false;
	g_dbus_connection_call(service, pair_agent, BT_AGENT_PATH, "org.bluez.Agent1",
	    "RequestPinCode", g_variant_new("(o)", "/org/bluez/hci0/dev_00_00_00_00_00_00"),
	    NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, agent_reject_reply, NULL);
	spin(); g_assert_true(agent_rejected);
	/* The right target from any sender other than BlueZ is rejected too. */
	g_autoptr(GDBusConnection) stranger = g_dbus_connection_new_for_address_sync(
	    g_getenv("DBUS_SYSTEM_BUS_ADDRESS"), G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
	    G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL, NULL, NULL);
	agent_rejected = false;
	g_dbus_connection_call(stranger, pair_agent, BT_AGENT_PATH, "org.bluez.Agent1",
	    "RequestPinCode", g_variant_new("(o)", pair_target), NULL,
	    G_DBUS_CALL_FLAGS_NONE, 1000, NULL, agent_reject_reply, NULL);
	spin(); g_assert_true(agent_rejected);
	fixture_bonded = true;
	g_dbus_method_invocation_return_value(pair_pending, NULL);
	g_clear_object(&pair_pending); spin(); spin();
	g_assert_false(app->bt_pair_active);
	g_assert_cmpuint(fixture_trust, ==, 1);
	g_assert_cmpuint(fixture_connect, ==, 1);
	g_assert_cmpuint(fixture_cancel, ==, 0);
	/* A misleading Pair success must not cause trust/connect without a bond. */
	fixture_bonded = false;
	bt_pair_start(app, "00:00:00:00:00:17", name); spin();
	g_dbus_method_invocation_return_value(pair_pending, NULL);
	g_clear_object(&pair_pending); spin();
	g_assert_nonnull(strstr(gtk_label_get_text(app->bluetooth_status), "No saved Bluetooth bond"));
	g_assert_cmpuint(fixture_trust, ==, 1);
	g_assert_cmpuint(fixture_connect, ==, 1);
	/* Leaving during Pair cancels, but never invokes CancelPairing afterwards. */
	bt_pair_start(app, "00:00:00:00:00:17", name); spin();
	gtk_stack_set_visible_child_name(app->stack, "settings"); spin();
	g_assert_cmpuint(fixture_cancel, ==, 1);
	g_assert_false(app->bt_pair_active);
	g_assert_false(app->bt_pair_busy);
	bt_pair_cancel(app);
	g_assert_cmpuint(fixture_cancel, ==, 1);
	assert_narrow(GTK_WIDGET(app->stack));
	g_assert_false(bt_valid_pin("1234567", true));
	g_assert_false(bt_valid_pin("12x", true));
	g_assert_true(bt_valid_pin("000123", true));
	gtk_widget_set_visible(GTK_WIDGET(app->window), FALSE); spin();
	g_dbus_connection_close_sync(stranger, NULL, NULL);
	g_free(pair_agent);
	g_print("PASS: long/hidden dialogs, visible PIN, bond gate, HID connect, wrong target/sender, cancellation\n");
}
