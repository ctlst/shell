from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_privacy_uses_retained_nodes_with_native_accessible_dismissal():
    source = (ROOT / "ctlstlock/ctlstlock.c").read_text()
    assert "gtk_snapshot_append_layout" in source
    assert "gtk_snapshot_append_linear_gradient" in source
    assert "cairo_" not in source
    assert 'gtk_button_new_with_label("Continue")' in source
    assert "GTK_ACCESSIBLE_PROPERTY_DESCRIPTION" in source
    assert "stops[i].color.alpha = 1;" in source
    assert "g_utf8_make_valid(text, -1)" in source
    assert "GDK_KEY_Return" in source and "GDK_KEY_space" in source
    assert 'g_getenv("GSK_RENDERER") == NULL' in source


def test_whole_sheet_motion_retains_input_and_cancels_cleanly():
    source = (ROOT / "ctlstlock/ctlstlock.c").read_text()
    assert 'window#ctlstlock { background: transparent; }' in source
    assert 'gsk_transform_translate(NULL, &point)' in source
    assert 'gtk_widget_add_controller(app->sheet, GTK_EVENT_CONTROLLER(drag))' in source
    assert 'g_signal_connect(drag, "cancel", G_CALLBACK(drag_cancel)' in source
    assert 'gtk_widget_add_tick_callback(app->sheet, settle_frame' in source
    assert '"gtk-enable-animations"' in source
    assert 'app->drag_active = app->dismissing = false;' in source
    assert 'app->settle_tick = 0;\n\tif (app->dismissing)\n\t\tset_visible_state(app, false);' in source
    assert 'height * 0.42' not in source
