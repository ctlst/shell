/* Reproduce boot before time sync without changing the host's clock. */
#define main home_application_main
#include "../ctlsthome/ctlsthome.c"
#undef main

static void check_date(struct home *home, int year, int month, int day,
    int days, int first_column)
{
	struct tm date = {.tm_year = year - 1900, .tm_mon = month - 1,
	    .tm_mday = day, .tm_hour = 12, .tm_isdst = -1};
	char title[64];
	int count = 0, selected = 0, weekdays = 0;

	mktime(&date);
	refresh_calendar(home, &date);
	strftime(title, sizeof(title), "%B %Y", &date);
	g_assert_cmpstr(gtk_label_get_text(home->calendar_title), ==, title);
	for (GtkWidget *child = gtk_widget_get_first_child(home->calendar_month);
	    child != NULL; child = gtk_widget_get_next_sibling(child)) {
		if (gtk_widget_has_css_class(child, "home-calendar-weekday"))
			weekdays++;
		if (!gtk_widget_has_css_class(child, "home-calendar-day"))
			continue;
		int value = atoi(gtk_label_get_text(GTK_LABEL(child)));
		int col, row, width, height;
		gtk_grid_query_child(GTK_GRID(home->calendar_month), child,
		    &col, &row, &width, &height);
		g_assert_cmpint(value, ==, ++count);
		g_assert_cmpint(col, ==, (first_column + value - 1) % 7);
		g_assert_cmpint(row, ==, 1 + (first_column + value - 1) / 7);
		if (gtk_widget_has_css_class(child, "home-calendar-today")) {
			selected++;
			g_assert_cmpint(value, ==, day);
		}
	}
	g_assert_cmpint(count, ==, days);
	g_assert_cmpint(selected, ==, 1);
	g_assert_cmpint(weekdays, ==, 7);
	GtkWidget *last = gtk_widget_get_last_child(home->calendar_month);
	g_object_ref(last);
	refresh_calendar(home, &date);
	g_assert_true(last == gtk_widget_get_last_child(home->calendar_month));
	g_object_unref(last);
}

int main(void)
{
	gtk_init();
	struct home home = {0};
	GtkWidget *card = g_object_ref_sink(calendar_card(&home));
	check_date(&home, 1970, 1, 1, 31, 3);
	check_date(&home, 2026, 10, 14, 31, 3); /* network sync */
	check_date(&home, 2026, 10, 15, 31, 3); /* midnight */
	check_date(&home, 2026, 11, 1, 30, 6); /* month rollover */
	check_date(&home, 2027, 1, 1, 31, 4); /* year rollover */
	check_date(&home, 2024, 2, 29, 29, 3); /* backward jump/leap year */
	check_date(&home, 2100, 2, 28, 28, 0); /* century exception */
	check_date(&home, 2000, 2, 29, 29, 1); /* leap century */
	g_object_unref(card);
	g_print("PASS: boot clock correction, midnight, month/year rollover, backward jumps, leap years, stable same-day refresh\n");
	return 0;
}
