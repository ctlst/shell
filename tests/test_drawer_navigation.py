"""Run drawer key policy without a graphical display."""
import ast
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock

SOURCE = (Path(__file__).resolve().parents[1] / "scripts/ctlstdrawer-ui").read_text()
GDK = SimpleNamespace(KEY_Escape=27, KEY_Return=13, KEY_KP_Enter=14, KEY_space=32,
					  KEY_Left=100, KEY_Right=101, KEY_Up=102, KEY_Down=103,
                      ModifierType=SimpleNamespace(CONTROL_MASK=1, ALT_MASK=2, SUPER_MASK=4),
                      keyval_to_unicode=lambda key: 0)


def method(name):
    cls = next(node for node in ast.parse(SOURCE).body
               if isinstance(node, ast.ClassDef) and node.name == "CtlstDrawer")
    node = next(node for node in cls.body if isinstance(node, ast.FunctionDef) and node.name == name)
    scope = {"Gdk": GDK, "Gtk": SimpleNamespace(ListScrollFlags=SimpleNamespace(FOCUS=1, SELECT=2))}
    exec(compile(ast.Module(body=[node], type_ignores=[]), "<drawer policy>", "exec"), scope)
    return scope[name]


def test_enter_on_search_launches_result_but_does_not_steal_native_button_or_grid_activation():
    search, root, grid, button = (Mock() for _ in range(4))
    app = SimpleNamespace(search=search, initial_focus=root, grid=grid,
                          get_focus=lambda: search, filtered=SimpleNamespace(get_n_items=lambda: 3),
                          launch_app=Mock(), search_engaged=False, on_search_changed=Mock())
    assert method("on_key_pressed")(app, None, GDK.KEY_Return, 0, 0)
    app.launch_app.assert_called_once_with(grid, 0)
    assert not method("on_key_pressed")(app, None, GDK.KEY_Return, 0, GDK.ModifierType.CONTROL_MASK)
    for target in (grid, button):
        target.is_ancestor.return_value = False
        app.get_focus = lambda: target
        app.launch_app.reset_mock()
        assert not method("on_key_pressed")(app, None, GDK.KEY_Return, 0, 0)
        app.launch_app.assert_not_called()
        assert not method("on_key_pressed")(app, None, GDK.KEY_space, 0, 0)


def test_immediate_enter_flushes_the_query_before_deciding_what_to_launch():
    search, grid = Mock(), Mock()
    matches = [1]
    app = SimpleNamespace(search=search, initial_focus=Mock(), grid=grid,
        get_focus=lambda: search, filtered=SimpleNamespace(get_n_items=lambda: matches[0]),
        launch_app=Mock(), on_search_changed=lambda _entry: matches.__setitem__(0, 0))
    assert method("on_key_pressed")(app, None, GDK.KEY_Return, 0, 0)
    app.launch_app.assert_not_called()
    app.on_search_changed = lambda _entry: matches.__setitem__(0, 1)
    assert method("on_key_pressed")(app, None, GDK.KEY_Return, 0, 0)
    app.launch_app.assert_called_once_with(grid, 0)


def test_empty_results_show_clear_search_and_restore_scroller():
    app = SimpleNamespace(favorite_ids=[], favorite_capacity=12, editing=False,
                          hidden_ids=set(),
                          filtered=SimpleNamespace(get_n_items=lambda: 0),
                          search=SimpleNamespace(get_text=lambda: "unknown"),
                          count=Mock(), scroller=Mock(), empty_state=Mock(),
                          empty_label=Mock(), clear_search=Mock())
    method("update_count")(app)
    app.empty_state.set_visible.assert_called_with(True)
    app.scroller.set_visible.assert_called_with(False)
    app.empty_label.set_text.assert_called_with("No matching apps")
    app.clear_search.set_visible.assert_called_with(True)
    app.filtered.get_n_items = lambda: 5
    method("update_count")(app)
    app.empty_state.set_visible.assert_called_with(False)
    app.scroller.set_visible.assert_called_with(True)


def test_typing_in_search_internal_text_widget_does_not_replace_existing_query():
    search, child = Mock(), Mock()
    child.is_ancestor.return_value = True
    app = SimpleNamespace(search=search, initial_focus=Mock(), get_focus=lambda: child)
    previous = GDK.keyval_to_unicode
    GDK.keyval_to_unicode = lambda key: key
    try:
        assert not method("on_key_pressed")(app, None, ord("a"), 0, 0)
        search.set_text.assert_not_called()
        search.grab_focus.assert_not_called()
    finally:
        GDK.keyval_to_unicode = previous


def test_drawer_feedback_is_visible_and_touch_scroller_is_preserved():
    assert "heading.set_visible(False)" not in SOURCE
    assert 'label="Clear search"' in SOURCE
    assert 'label="Close"' in SOURCE
    assert "self.touch_scroller = TouchScroller(" in SOURCE
    assert "scroller.set_kinetic_scrolling(True)" in SOURCE
    assert "self.grid.set_max_columns(1 if self.editing else 8)" in SOURCE
    assert "rebuild_favorites_preview" not in SOURCE
    assert "tile.hide_badge.set_visible(self.editing)" in SOURCE
    assert 'pin_button.connect("clicked", self.on_pin_clicked)' in SOURCE
    assert "self.bound_tiles.discard(tile)" in SOURCE


def test_edit_mode_reveals_hidden_entries_and_does_not_launch_apps():
    app = SimpleNamespace(editing=False, hidden_ids={"hidden.desktop"},
        search=SimpleNamespace(get_text=lambda: "example"))
    entry = SimpleNamespace(app=SimpleNamespace(get_id=lambda: "hidden.desktop"),
                            search_text="example app")
    assert not method("filter_app")(app, entry)
    app.editing = True
    assert method("filter_app")(app, entry)
    app.search.get_text = lambda: "different"
    assert not method("filter_app")(app, entry)
    # Editing short-circuits before the launch helper or model is touched.
    method("launch_app")(SimpleNamespace(editing=True), None, 0)


def test_edit_rows_have_reversible_visibility_and_no_second_home_grid():
    assert 'operation = "remove" if desktop_id in self.hidden_ids else "add"' in SOURCE
    assert 'tile.hide_badge.set_label("Show" if hidden else "Hide")' in SOURCE
    assert "rebuild_favorites_preview" not in SOURCE
    assert "tile.favorite_badge.set_visible(not self.editing)" in SOURCE
    assert "tile.app_label.set_hexpand(self.editing)" in SOURCE


def test_browse_density_is_scoped_to_normal_tiles_and_remains_theme_editable():
    css = (Path(__file__).resolve().parents[1] / "themes/components.css").read_text()
    for source in (SOURCE, css):
        assert "window#ctlstdrawer .app-tile:not(.drawer-edit-row)" in source
        assert "min-width: 88px; min-height: 112px; padding: 6px 4px; margin: 0;" in source
        assert "min-width: 44px; min-height: 44px; padding: 6px; margin: 0;" in source
    assert "ICON_DISPLAY_SIZE = 44" in SOURCE


def test_only_committed_drawer_owns_keyboard():
    cls = next(node for node in ast.parse(SOURCE).body
               if isinstance(node, ast.ClassDef) and node.name == "DrawerApplication")
    function = next(node for node in cls.body
                    if isinstance(node, ast.FunctionDef) and node.name == "show_window")
    layer = SimpleNamespace(set_keyboard_mode=Mock(),
                            KeyboardMode=SimpleNamespace(EXCLUSIVE=1, NONE=0))
    scope = {"Gtk4LayerShell": layer, "GLib": SimpleNamespace(idle_add=Mock()),
             "os": SimpleNamespace(path=SimpleNamespace(exists=lambda _: False))}
    exec(compile(ast.Module(body=[function], type_ignores=[]), "<drawer ownership>", "exec"), scope)
    window = Mock(drop_entry=None)
    app = SimpleNamespace(window=window, visible_file="fixture", set_visible_state=Mock())
    for focus, mode in ((True, 1), (False, 0)):
        scope["show_window"](app, focus=focus)
        layer.set_keyboard_mode.assert_called_with(window, mode)
