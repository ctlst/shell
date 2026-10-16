/* Real BlueZ object-manager protocol on a PRIVATE test bus; no radio access. */
#define main shade_application_main
#include "../ctlstshade/ctlstshade.c"
#undef main

static unsigned starts, stops;
static bool delay_start, fail_start;
static GDBusMethodInvocation *pending;
static const char *adapter_path = "/org/bluez/hci0";

#include "bluetooth-pairing-fixture.h"

static void
method(GDBusConnection *connection, const char *sender, const char *path,
    const char *interface, const char *name, GVariant *args,
    GDBusMethodInvocation *call, gpointer data)
{
	(void)connection; (void)sender; (void)path; (void)interface;
	(void)args; (void)data;
	if (pairing_method(connection, sender, name, args, call)) return;
	if (strcmp(name, "GetManagedObjects") == 0) {
		GVariantBuilder objects, interfaces, props;
		g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
		g_variant_builder_init(&interfaces, G_VARIANT_TYPE("a{sa{sv}}"));
		g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
		g_variant_builder_add(&props, "{sv}", "Powered", g_variant_new_boolean(TRUE));
		g_variant_builder_add(&interfaces, "{sa{sv}}", "org.bluez.Adapter1", &props);
		g_variant_builder_add(&objects, "{oa{sa{sv}}}", adapter_path, &interfaces);
		for (unsigned i = 0; i < 24; i++) {
			g_autofree char *device_path = g_strdup_printf("/org/bluez/hci0/dev_00_00_00_00_00_%02X", i);
			g_autofree char *address = g_strdup_printf("00:00:00:00:00:%02X", i);
			g_variant_builder_init(&interfaces, G_VARIANT_TYPE("a{sa{sv}}"));
			g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
			g_variant_builder_add(&props, "{sv}", "Address", g_variant_new_string(address));
			g_variant_builder_add(&props, "{sv}", "Alias", g_variant_new_string(address));
			g_variant_builder_add(&props, "{sv}", "Icon", g_variant_new_string("input-keyboard"));
			const char *uuids[] = {"00001124-0000-1000-8000-00805f9b34fb", NULL};
			g_variant_builder_add(&props, "{sv}", "UUIDs", g_variant_new_strv(uuids, -1));
			g_variant_builder_add(&interfaces, "{sa{sv}}", "org.bluez.Device1", &props);
			g_variant_builder_add(&objects, "{oa{sa{sv}}}", device_path, &interfaces);
		}
		g_dbus_method_invocation_return_value(call,
		    g_variant_new("(a{oa{sa{sv}}})", &objects));
	} else if (strcmp(name, "StartDiscovery") == 0) {
		starts++;
		if (fail_start) {
			g_dbus_method_invocation_return_dbus_error(call, "org.bluez.Error.Failed", "Test failure");
		} else if (delay_start) {
			pending = g_object_ref(call);
		} else {
			g_dbus_method_invocation_return_value(call, NULL);
		}
	} else {
		g_assert_cmpstr(name, ==, "StopDiscovery");
		stops++;
		g_dbus_method_invocation_return_value(call, NULL);
	}
}

static void
spin(void)
{
	gint64 end = g_get_monotonic_time() + 300000;
	do {
		while (g_main_context_iteration(NULL, FALSE)) {}
		g_usleep(1000);
	} while (g_get_monotonic_time() < end);
}

int
main(void)
{
	gtk_init();
	g_autoptr(GTestDBus) bus = g_test_dbus_new(G_TEST_DBUS_NONE);
	g_test_dbus_up(bus);
	g_setenv("DBUS_SYSTEM_BUS_ADDRESS", g_test_dbus_get_bus_address(bus), TRUE);
	g_autoptr(GError) error = NULL;
	g_autoptr(GDBusConnection) service = g_dbus_connection_new_for_address_sync(
	    g_test_dbus_get_bus_address(bus), G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
	    G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL, NULL, &error);
	g_assert_no_error(error);
	g_autoptr(GVariant) owned = g_dbus_connection_call_sync(service,
	    "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
	    "RequestName", g_variant_new("(su)", "org.bluez", 0), NULL,
	    G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
	g_assert_no_error(error);
	const char *xml = "<node><interface name='org.freedesktop.DBus.ObjectManager'>"
	    "<method name='GetManagedObjects'><arg direction='out' type='a{oa{sa{sv}}}'/></method>"
	    "</interface><interface name='org.bluez.Adapter1'>"
	    "<method name='StartDiscovery'/><method name='StopDiscovery'/>"
	    "</interface></node>";
	g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(xml, &error);
	g_assert_no_error(error);
	const GDBusInterfaceVTable vtable = {.method_call = method};
	g_dbus_connection_register_object(service, "/", info->interfaces[0], &vtable, NULL, NULL, &error);
	g_assert_no_error(error);
	g_dbus_connection_register_object(service, adapter_path, info->interfaces[1], &vtable, NULL, NULL, &error);
	g_assert_no_error(error);
	struct app app = {0};
	pairing_register(service);
	app.window = GTK_WINDOW(gtk_window_new());
	gtk_layer_init_for_window(app.window);
	app.stack = GTK_STACK(gtk_stack_new());
	gtk_stack_set_hhomogeneous(app.stack, FALSE);
	gtk_stack_set_vhomogeneous(app.stack, FALSE);
	GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	app.bluetooth_status = GTK_LABEL(gtk_label_new(""));
	app.bluetooth_list = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_box_append(GTK_BOX(page), GTK_WIDGET(app.bluetooth_status));
	gtk_box_append(GTK_BOX(page), GTK_WIDGET(app.bluetooth_list));
	gtk_stack_add_named(app.stack, page, "bluetooth");
	gtk_stack_add_named(app.stack, gtk_label_new("Settings"), "settings");
	gtk_window_set_child(app.window, GTK_WIDGET(app.stack));
	g_signal_connect(app.stack, "notify::visible-child-name", G_CALLBACK(bt_visibility_changed), &app);
	g_signal_connect(app.window, "notify::visible", G_CALLBACK(bt_visibility_changed), &app);
	gtk_window_present(app.window);
	for (unsigned i = 0; i < 20 && !app.bt_scanning; i++) spin();
	g_assert_true(app.bt_scanning);
	g_assert_cmpuint(starts, ==, 1);
	g_assert_cmpuint(g_hash_table_size(app.bt_rows), ==, 24);
	const char *target = "/org/bluez/hci0/dev_00_00_00_00_00_17";
	GtkWidget *row = g_hash_table_lookup(app.bt_rows, target);
	g_assert_nonnull(strstr(gtk_button_get_label(GTK_BUTTON(row)), "Unknown device"));
	GVariantBuilder changed;
	g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&changed, "{sv}", "Name", g_variant_new_string("Targus Multi-Device Compact Bluetooth Keyboard"));
	g_variant_builder_add(&changed, "{sv}", "Paired", g_variant_new_boolean(TRUE));
	g_variant_builder_add(&changed, "{sv}", "Connected", g_variant_new_boolean(TRUE));
	g_dbus_connection_emit_signal(service, NULL, target,
	    "org.freedesktop.DBus.Properties", "PropertiesChanged",
	    g_variant_new("(sa{sv}as)", "org.bluez.Device1", &changed, NULL), &error);
	g_assert_no_error(error);
	spin(); spin();
	g_assert_true(row == g_hash_table_lookup(app.bt_rows, target));
	g_assert_nonnull(strstr(gtk_button_get_label(GTK_BUTTON(row)), "Targus"));
	g_assert_true(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "connected")));
	gtk_stack_set_visible_child_name(app.stack, "settings"); spin();
	g_assert_false(app.bt_scanning);
	g_assert_cmpuint(stops, ==, 1);
	delay_start = true;
	gtk_stack_set_visible_child_name(app.stack, "bluetooth"); spin();
	g_assert_nonnull(pending);
	gtk_widget_set_visible(GTK_WIDGET(app.window), FALSE);
	g_dbus_method_invocation_return_value(pending, NULL);
	g_clear_object(&pending); spin();
	g_assert_false(app.bt_scanning);
	g_assert_cmpuint(stops, ==, 2);
	delay_start = false; fail_start = true;
	gtk_window_present(app.window); spin();
	g_assert_nonnull(strstr(gtk_label_get_text(app.bluetooth_status), "Test failure"));
	g_assert_false(app.bt_scanning);
	fail_start = false;
	bt_reconcile(&app); spin();
	g_assert_true(app.bt_scanning);
	gtk_widget_set_visible(GTK_WIDGET(app.window), FALSE); spin();
	g_assert_cmpuint(stops, ==, 3);
	pairing_tests(&app, service);
	g_clear_object(&app.bt_manager);
	g_clear_pointer(&app.bt_rows, g_hash_table_unref);
	gtk_window_destroy(app.window);
	g_dbus_connection_close_sync(service, NULL, NULL);
	g_test_dbus_down(bus);
	g_print("PASS: 24 devices, delayed name, stable row, state updates, page exit, hide-during-start, error/retry\n");
	return 0;
}
