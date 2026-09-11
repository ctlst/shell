"""Capability and completion contracts for optional Quick Settings helpers."""

from pathlib import Path


SHADE = (Path(__file__).resolve().parents[1] / "ctlstshade/ctlstshade.c").read_text()


def function(name):
    return SHADE.split(f"\n{name}(", 1)[1].split("\n}\n", 1)[0]


def test_helper_capabilities_require_executable_files_in_installed_runtime():
    body = function("script_available")
    assert "ctlst_script_path(name)" in body
    assert "G_FILE_TEST_IS_REGULAR" in body
    assert "access(path, X_OK) == 0" in body


def test_rotation_is_unavailable_without_helper_even_with_stale_lock_state():
    body = function("update_rotation_button")
    assert 'script_available("auto-rotate")' in body
    assert "available && app->rotation_locked" in body
    assert "available && !app->rotation_action_running" in body
    assert '!available ? "Unavailable"' in body
    assert 'if (!script_available("auto-rotate"))' in function("action_rotate")


def test_sound_profiles_are_disabled_and_explained_without_helper():
    body = function("build_sound")
    assert '"Sound profiles unavailable"' in body
    assert 'gtk_widget_set_sensitive(profiles, script_available("sound-profile"))' in body
    assert 'if (!script_available("sound-profile"))' in function("set_sound_profile")


def test_sound_success_is_only_reported_after_successful_exit():
    body = function("set_sound_profile")
    assert "g_subprocess_wait_check_async" in body
    assert '"Sound profile updated"' not in body
    finish = function("finish_sound_action")
    assert "g_subprocess_wait_check_finish" in finish
    assert 'success ? "Sound profile updated" : "Could not change sound profile"' in finish
    assert "run_detached" not in function("action_sound_profile")
    assert "gtk_widget_add_css_class" not in function("action_sound")


def test_sound_actions_are_serialized_until_completion():
    body = function("set_sound_profile")
    assert body.index("if (app->sound_action_running)") < body.index("g_subprocess_new")
    assert "app->sound_action_running = true" in body
    assert "gtk_widget_set_sensitive(app->sound_profiles, FALSE)" in body
    finish = function("finish_sound_action")
    assert "app->sound_action_running = false" in finish
    assert "gtk_widget_set_sensitive(app->sound_profiles," in finish
    assert 'script_available("sound-profile")' in finish


def test_missing_sound_helper_opens_visible_explanation_from_top_tile():
    body = function("action_sound")
    assert 'if (!script_available("sound-profile"))' in body
    assert "show_sound(app);\n\t\treturn;" in body
    assert body.index("show_sound(app)") < body.index('set_sound_profile(app, "cycle")')
