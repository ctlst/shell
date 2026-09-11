from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_edit_preview_uses_a_real_input_transform_and_separate_controls():
    frame = (ROOT / "ctlsthome/home-edit-frame.h").read_text()
    main = (ROOT / "ctlsthome/ctlsthome.c").read_text()
    assert "gsk_transform_scale(transform, frame->scale, frame->scale)" in frame
    assert "gtk_widget_allocate(frame->scene, width, height, baseline, transform)" in frame
    assert "gtk_widget_measure(frame->controls" in frame
    assert "home_edit_frame_arrange_controls(frame, width, height)" in frame
    assert "sum + chrome <= width - 2 * frame->inset" in frame
    assert "gtk_overlay_add_overlay(home->edit_scene," in main
    assert "gtk_box_append(GTK_BOX(home->edit_controls), toolbar)" in main
    assert "gtk_box_append(GTK_BOX(home->edit_controls), page_bar)" in main
    assert "gtk_overlay_add_overlay(home->context_overlay, toolbar)" not in main
    assert "offset_x /= home->edit_frame->scale" in main
    assert "offset_y /= home->edit_frame->scale" in main
    assert "!point_in_edit_controls(home, ox, oy)" in main
    assert "editing ? GTK_PHASE_CAPTURE : GTK_PHASE_BUBBLE" in main


def test_edit_spacing_is_documented_and_reloadable():
    source = (ROOT / "ctlsthome/home-config.c").read_text()
    example = (ROOT / "ctlsthome/home.yaml.example").read_text()
    assert "cfg->edit_inset_px = 12" in source
    assert "cfg->edit_controls_gap_px = 12" in source
    assert 'strcmp(section, "edit")' in source
    assert "inset_px: 12" in example
    assert "controls_gap_px: 12" in example


def test_edit_controls_have_themeable_dock_clearance_and_labeled_page_actions():
    main = (ROOT / "ctlsthome/ctlsthome.c").read_text()
    css = (ROOT / "themes/components.css").read_text()
    assert 'gtk_widget_add_css_class(home->edit_controls, "home-edit-controls")' in main
    for source in (main, css):
        assert ".home-edit-controls { padding-bottom: 28px; }" in source
        assert ".home-launcher-page-bar button" in source
        assert "min-width: 44px;" in source
    assert 'GTK_ACCESSIBLE_PROPERTY_LABEL, "Add Home page"' in main
    assert 'GTK_ACCESSIBLE_PROPERTY_LABEL, "Remove current Home page"' in main
    assert "gtk_accessible_reset_relation(GTK_ACCESSIBLE(home->launcher_page_add)" in main
    assert "gtk_accessible_reset_relation(GTK_ACCESSIBLE(home->launcher_page_remove)" in main
    assert "home->launcher_page_count < home->config.page_max_pages" in main

def test_widget_picker_is_bounded_without_resizing_the_home_grid():
    main = (ROOT / "ctlsthome/ctlsthome.c").read_text()
    host = (ROOT / "ctlsthome/home-picker-host.h").read_text()
    assert "home_picker_host_new(picker)" in main
    assert "home->widget_picker = picker_host" in main
    assert "gtk_overlay_add_overlay(home->context_overlay, picker_host)" in main
    assert "gtk_widget_set_size_request(picker, 340, 320)" not in main
    assert "*minimum = *natural = 0" in host
    assert "MIN(340, width - 24)" in host
    assert "MIN(320, height - 24)" in host
    assert "gsk_transform_scale" not in host
    assert "gtk_widget_snapshot_child" in host
    assert "gtk_label_set_ellipsize(choice_label, PANGO_ELLIPSIZE_END)" in main


def test_open_picker_blocks_launcher_cell_and_remove_badge_fallbacks():
    main = (ROOT / "ctlsthome/ctlsthome.c").read_text()
    for function in ("home_launcher_icon_at", "launcher_remove_badge_at"):
        body = main.split(function + "(struct home *home, double x, double y)\n{", 1)[1].split("\n}\n", 1)[0]
        guard = "home->widget_picker != NULL && gtk_widget_get_visible(home->widget_picker)"
        assert guard in body
        assert body.index(guard) < body.index("for (")


def test_remove_badge_uses_cell_corner_not_artwork_disc():
    main = (ROOT / "ctlsthome/ctlsthome.c").read_text()
    body = main.split("launcher_icon_button(struct home *home, GIcon *app_icon, const char *desktop_id)\n{", 1)[1].split("\n}\n", 1)[0]
    assert "gtk_widget_set_halign(icon_host, GTK_ALIGN_FILL)" in body
    assert "gtk_widget_set_valign(icon_host, GTK_ALIGN_FILL)" in body
    assert "gtk_button_set_child(GTK_BUTTON(button), icon_host)" in body
    assert "gtk_widget_set_halign(disc, GTK_ALIGN_CENTER)" in body
    assert "gtk_widget_set_valign(disc, GTK_ALIGN_CENTER)" in body
    assert "gtk_widget_set_halign(remove_badge, GTK_ALIGN_END)" in body
    assert "gtk_widget_set_valign(remove_badge, GTK_ALIGN_START)" in body


def test_widget_edit_controls_have_small_marks_and_guarded_release():
    main = (ROOT / "ctlsthome/ctlsthome.c").read_text()
    css = (ROOT / "themes/components.css").read_text()
    for source in (main, css):
        assert "button.home-widget-remove > label" in source
        assert "min-width: 22px; min-height: 22px;" in source.replace("\n  ", " ")
        assert "button.home-widget-remove" in source
    assert 'g_strdup_printf("Remove %s widget", home_widget_display_name(item))' in main
    assert 'g_strdup_printf("Resize %s widget", home_widget_display_name(item))' in main
    body = main.split("home_widget_remove_clicked(GtkGestureClick *gesture, int presses, double x,\n    double y, gpointer data)\n{", 1)[1].split("\n}\n", 1)[0]
    for guard in ("!item->home->layout_editing", "!gtk_widget_get_mapped", "x < 0", "y < 0",
                  "x >= gtk_widget_get_width", "y >= gtk_widget_get_height"):
        assert guard in body
        assert body.index(guard) < body.index("GTK_EVENT_SEQUENCE_CLAIMED")


def test_apps_icon_has_standard_grid_fallback():
    main = (ROOT / "ctlsthome/ctlsthome.c").read_text()
    body = main.split('favorite_icon_for_id(const char *favorite)\n{', 1)[1].split('\n}\n', 1)[0]
    assert '"view-app-grid-symbolic"' in body
    assert 'gtk_icon_theme_has_icon(theme, "dev.ctlst.Apps")' in body
