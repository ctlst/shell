#define main home_application_main
#include "../ctlsthome/ctlsthome.c"
#undef main

int main(void)
{
	gtk_init();
	struct home home={0};
	home_config_set_defaults(&home.config);
	home.clock=GTK_LABEL(g_object_ref_sink(gtk_label_new("before")));
	home.date=GTK_LABEL(g_object_ref_sink(gtk_label_new("before")));
	GtkWidget *calendar=g_object_ref_sink(calendar_card(&home));
	GtkWidget *glance=g_object_ref_sink(glance_card(&home));
	home.grid_item_count=2;
	home.grid_items[0]=(struct home_grid_item){.id="calendar",.page=0};
	home.grid_items[1]=(struct home_grid_item){.id="glance",.page=0};
	home.visible=true;
	struct tm epoch={.tm_year=70,.tm_mon=0,.tm_mday=1,.tm_hour=12,.tm_isdst=-1};
	mktime(&epoch);refresh_calendar(&home,&epoch);
	home.page_drag_active=true;
	refresh_clock(&home);
	g_assert_cmpstr(gtk_label_get_text(home.clock),==,"before");
	g_assert_cmpint(home.calendar_date.tm_year,==,70);
	g_assert_true(home.clock_dirty);
	home.page_drag_active=false;home.page_drag_owned=true;
	refresh_clock(&home);
	g_assert_cmpint(home.calendar_date.tm_year,==,70);
	home.page_drag_owned=false;
	refresh_clock(&home);
	g_assert_cmpint(home.calendar_date.tm_year,>,70);
	g_assert_false(home.clock_dirty);
	for(int columns=1;columns<=3;columns++) {
		layout_glance(&home,columns,true);
		for(int i=0;i<6;i++) {
			int x,y,w,h;
			gtk_grid_query_child(GTK_GRID(home.glance_grid),home.glance_cells[i],&x,&y,&w,&h);
			g_assert_cmpint(x,==,i%columns);g_assert_cmpint(y,==,i/columns);
		}
	}
	set_glance_value(&home,0,"50%","Charging");
	g_assert_cmpstr(gtk_label_get_text(home.battery_context),==,"50%");
	g_object_unref(calendar);g_object_unref(glance);
	g_object_unref(home.clock);g_object_unref(home.date);
	g_print("PASS: clock/calendar frozen through drag and settle, catch-up at rest, responsive six-field status layout\n");
	return 0;
}
