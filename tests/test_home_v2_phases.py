from pathlib import Path
import subprocess
import tempfile
import textwrap
import os

ROOT = Path(__file__).resolve().parents[1]
HOME = ROOT / "ctlsthome"


def _glib_flags():
    cflags = subprocess.check_output(
        ["pkg-config", "--cflags", "glib-2.0"], text=True
    ).strip()
    libs = subprocess.check_output(
        ["pkg-config", "--libs", "glib-2.0"], text=True
    ).strip()
    return cflags, libs


def test_home_pager_snap_and_fling_math():
    try:
        cflags, libs = _glib_flags()
    except (subprocess.CalledProcessError, FileNotFoundError):
        import pytest

        pytest.skip("glib-2.0 pkg-config not available on this host")

    harness = textwrap.dedent(
        r"""
        #include "home-pager.h"
        #include <stdio.h>
        int main(void) {
          if (home_pager_commit_page(0, 3, -0.49, 0.0, 0.5, 0.35) != 0)
            return 2;
          if (home_pager_commit_page(0, 3, -0.50, 0.0, 0.5, 0.35) != 1)
            return 3;
          if (home_pager_commit_page(1, 3, 0.50, 0.0, 0.5, 0.35) != 0)
            return 4;
          /* Fling overrides small offset. */
          if (home_pager_commit_page(0, 3, -0.10, -0.40, 0.5, 0.35) != 1)
            return 5;
          if (home_pager_commit_page(2, 3, 0.10, 0.40, 0.5, 0.35) != 1)
            return 6;
          bool done = false;
          double p = home_pager_settle_progress(0, 110000, 220, &done);
          if (done || p <= 0.0 || p >= 1.0)
            return 7;
          p = home_pager_settle_progress(0, 220000, 220, &done);
          if (!done || p < 1.0)
            return 8;
          return 0;
        }
        """
    )
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "pager_test.c"
        src.write_text(harness)
        out = Path(td) / "pager_test"
        cmd = (
            f"cc -std=c11 -Wall -Wextra -Werror {cflags} -I{HOME} "
            f"-o {out} {src} {HOME}/home-pager.c -lm {libs}"
        )
        subprocess.check_call(cmd, shell=True)
        subprocess.check_call([str(out)])


def test_home_rotation_cluster_and_strip_mapping():
    try:
        cflags, libs = _glib_flags()
    except (subprocess.CalledProcessError, FileNotFoundError):
        import pytest

        pytest.skip("glib-2.0 pkg-config not available on this host")

    harness = textwrap.dedent(
        r"""
        #include "home-rotation.h"
        #include <math.h>
        #include <stdio.h>
        int main(void) {
          int vc, vr, lc, lr;
          bool strip = false;
          /* Portrait identity in cluster. */
          if (!home_rotation_map_cell(false, 5, 7, 5, 2, 3, &vc, &vr, &strip)
              || strip || vc != 2 || vr != 3)
            return 2;
          /* Landscape keeps the widget/cluster composition upright. */
          if (!home_rotation_map_cell(true, 5, 7, 5, 2, 3, &vc, &vr, &strip)
              || strip || vc != 2 || vr != 3)
            return 3;
          if (!home_rotation_unmap_cell(true, 5, 7, 5, false, 2, 3, &lc, &lr)
              || lc != 2 || lr != 3)
            return 4;
          /* Strip portrait local row. */
          if (!home_rotation_map_cell(false, 5, 7, 5, 4, 6, &vc, &vr, &strip)
              || !strip || vc != 4 || vr != 1)
            return 5;
          /* Strip landscape side dock. */
          if (!home_rotation_map_cell(true, 5, 7, 5, 4, 6, &vc, &vr, &strip)
              || !strip || vc != 1 || vr != 4)
            return 6;
          int c = 0, r = 4, cs = 5, rs = 3;
          if (!home_rotation_clamp_widget(5, 5, &c, &r, &cs, &rs)
              || r != 4 || rs != 1 || cs != 5)
            return 7;
          if (fabs(home_rotation_zoom_out_scale(0, 90, 0.91) - 1.0) > 0.0001)
            return 8;
          if (fabs(home_rotation_zoom_out_scale(90, 90, 0.91) - 0.91) > 0.0001)
            return 9;
          if (fabs(home_rotation_spring_scale(0, 140, 0.94, 1.015) - 0.94) > 0.0001)
            return 10;
          if (home_rotation_spring_scale(95.2, 140, 0.94, 1.015) < 1.014)
            return 11;
          if (fabs(home_rotation_spring_scale(140, 140, 0.94, 1.015) - 1.0) > 0.0001)
            return 12;
          return 0;
        }
        """
    )
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "rot_test.c"
        src.write_text(harness)
        out = Path(td) / "rot_test"
        cmd = (
            f"cc -std=c11 -Wall -Wextra -Werror {cflags} -I{HOME} "
            f"-o {out} {src} {HOME}/home-rotation.c -lm {libs}"
        )
        subprocess.check_call(cmd, shell=True)
        subprocess.check_call([str(out)])


def test_home_widget_descriptor_sample_and_validation():
    try:
        cflags, libs = _glib_flags()
    except (subprocess.CalledProcessError, FileNotFoundError):
        import pytest

        pytest.skip("glib-2.0 pkg-config not available on this host")

    sample = ROOT / "widgets" / "notes.yaml"
    assert sample.is_file()
    harness = textwrap.dedent(
        r"""
        #include "home-widget-desc.h"
        #include <stdio.h>
        #include <string.h>
        int main(int argc, char **argv) {
          struct home_widget_desc d;
          const char *path = argc > 1 ? argv[1] : NULL;
          if (path == NULL)
            return 2;
          if (!home_widget_desc_load_file(&d, path))
            return 3;
          if (strcmp(d.id, "notes") != 0 || d.kind != HOME_WIDGET_KIND_EXEC)
            return 4;
          if (d.min_columns != 2 || d.default_rows != 2)
            return 5;
          if (strcmp(d.protocol, "ctlst-widget-1") != 0 ||
              strcmp(d.interaction, "launch") != 0)
            return 8;
          if (home_widget_desc_is_builtin_id("clock") != true)
            return 6;
          /* Builtin id collision for exec must fail. */
          {
            FILE *fp = fopen("bad.yaml", "w");
            fputs("id: clock\nname: X\nkind: exec\nmin_columns: 1\n"
                  "min_rows: 1\nexec: /bin/true\n", fp);
            fclose(fp);
            if (home_widget_desc_load_file(&d, "bad.yaml"))
              return 7;
          }
          /* Malformed and overflowing dimensions must not be silently kept. */
          {
            FILE *fp = fopen("bad-number.yaml", "w");
            fputs("id: bad-number\nname: Bad Number\nkind: exec\n"
                  "protocol: ctlst-widget-1\ninteraction: none\n"
                  "min_columns: 999999999999999999999\nmin_rows: 1\n"
                  "exec: /bin/true\n", fp);
            fclose(fp);
            if (home_widget_desc_load_file(&d, "bad-number.yaml"))
              return 9;
          }
          return 0;
        }
        """
    )
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "desc_test.c"
        src.write_text(harness)
        out = Path(td) / "desc_test"
        cmd = (
            f"cc -std=c11 -Wall -Wextra -Werror {cflags} -I{HOME} "
            f"-o {out} {src} {HOME}/home-widget-desc.c {libs}"
        )
        subprocess.check_call(cmd, shell=True, cwd=td)
        subprocess.check_call([str(out), str(sample)], cwd=td)


def test_home_v2_phases_wired_in_sources():
    main = (HOME / "ctlsthome.c").read_text()
    makefile = (HOME / "Makefile").read_text()
    assert "home-pager.h" in main
    assert "home_page_drag_begin" in main
    assert "home-rotation.h" in main
    assert "strip_grid" in main
    assert "home_widget_desc_scan_dir" in main
    assert 'visibility"' in main or "visibility" in main
    assert "home-rotation.c" in makefile
    assert "home-widget-desc.c" in makefile
    assert "home-widget-runtime.c" in makefile
    assert "add_external_widgets" in main
    assert (HOME / "HOME-V2.md").is_file()


def test_page_swipe_can_start_on_icons_and_widgets_without_arming_edit():
    main = (HOME / "ctlsthome.c").read_text()
    begin = main.split(
        "home_page_drag_begin(GtkGestureDrag *gesture", maxsplit=1
    )[1].split("static void\nhome_page_drag_update", maxsplit=1)[0]
    update = main.split(
        "home_page_drag_update(GtkGestureDrag *gesture", maxsplit=1
    )[1].split("static void\nhome_page_drag_end", maxsplit=1)[0]
    icon_begin = main.split(
        "home_widget_drag_begin(GtkGestureDrag *gesture", maxsplit=1
    )[1].split("static void\nhome_widget_drag_update", maxsplit=1)[0]

    assert "home_launcher_icon_at(home, start_x, start_y)" not in begin
    assert "home_grid_item_at(home, start_x, start_y" not in begin
    assert "home->pending_launcher_press != NULL" not in begin
    assert "distance >= HOME_TAP_SLOP" in update
    assert "cancel_launcher_drag_arm(home)" in update
    assert "pager_page_stride(home) * 0.04" in update
    assert "home->page_release_suppress = true" in update
    icon_branch = icon_begin.split("if (icon != NULL) {", maxsplit=1)[1]
    icon_branch = icon_branch.split("\n\t}", maxsplit=1)[0]
    # Normal mode leaves the sequence shared so horizontal page swipes still
    # win. Explicit edit mode, however, must claim and move immediately.
    assert "if (home->layout_editing)" in icon_branch
    assert "start_launcher_icon_drag(home, icon)" in icon_branch
    assert "GTK_EVENT_SEQUENCE_CLAIMED" in icon_branch
    assert "g_timeout_add(" in icon_branch


def test_gsk_pager_moves_a_retained_scene_on_the_frame_clock():
    main = (HOME / "ctlsthome.c").read_text()
    allocate = main.split(
        "home_pager_motion_size_allocate(GtkWidget *widget", maxsplit=1
    )[1].split("static void\nhome_pager_motion_snapshot", maxsplit=1)[0]
    snapshot = main.split(
        "home_pager_motion_snapshot(GtkWidget *widget", maxsplit=1
    )[1].split("static void\nhome_pager_motion_dispose", maxsplit=1)[0]
    translate = main.split(
        "home_pager_motion_set_translation(HomePagerMotion *motion", maxsplit=1
    )[1].split("struct home_grid_item", maxsplit=1)[0]
    assert "gtk_widget_allocate(motion->child" in allocate
    assert "gsk_transform_translate(NULL, &offset)" in allocate
    assert "gtk_snapshot_translate" not in snapshot
    assert "gtk_snapshot_push_clip(snapshot, &viewport)" in snapshot
    assert "gtk_snapshot_pop(snapshot)" in snapshot
    assert snapshot.index("gtk_snapshot_push_clip") < snapshot.index(
        "gtk_widget_snapshot_child"
    ) < snapshot.index("gtk_snapshot_pop")
    assert "GTK_OVERFLOW_HIDDEN" in main.split(
        "home_pager_motion_init(HomePagerMotion *motion)", maxsplit=1
    )[1].split("static HomePagerMotion *", maxsplit=1)[0]
    assert "gtk_widget_queue_allocate(GTK_WIDGET(motion))" in translate
    settle = main.split(
        "static void\nstart_page_settle(struct home *home", maxsplit=1
    )[1].split("static void\nhome_page_drag_begin", maxsplit=1)[0]
    assert "gtk_widget_add_tick_callback" in settle
    assert "g_timeout_add" not in settle


def test_gsk_pager_uses_full_output_stride_with_a_retained_gutter():
    main = (HOME / "ctlsthome.c").read_text()
    stride = main.split(
        "pager_page_stride(struct home *home)", maxsplit=1
    )[1].split("static int\npager_viewport_height", maxsplit=1)[0]
    sync = main.split(
        "sync_page_panel_widths(struct home *home)", maxsplit=1
    )[1].split("static void\nensure_page_panels", maxsplit=1)[0]

    assert "home->rotation_host" in stride
    assert "MAX(viewport, output)" in stride
    assert "HOME_PAGER_SEAM_GUARD_PX" in stride
    assert "page_gap = MAX(stride - width, 0)" in sync
    assert "gtk_box_set_spacing(GTK_BOX(home->page_strip), page_gap)" in sync
    assert "home_pager_motion_set_geometry" in sync


def test_gsk_pager_width_comes_from_the_widget_that_moves():
    main = (HOME / "ctlsthome.c").read_text()
    width = main.split(
        "pager_viewport_width(struct home *home)", maxsplit=1
    )[1].split("static int\npager_page_stride", maxsplit=1)[0]

    assert "home->pager_gsk && home->page_motion != NULL" in width
    assert "gtk_widget_get_width(GTK_WIDGET(home->page_motion))" in width
    assert width.index("home->page_motion") < width.index("home->page_viewport_pad")
    assert "home->page_motion->prepare = prepare_home_pager_allocation;" in main


def test_gsk_pager_allocates_height_without_persisting_the_old_minimum():
    main = (HOME / "ctlsthome.c").read_text()
    sync = main.split("sync_page_panel_widths(struct home *home)", 1)[1].split(
        "static void\nensure_page_panels", 1)[0]
    assert "int request_height = home->pager_gsk ? -1 : height;" in sync
    assert "gtk_widget_set_size_request(panel, width, request_height);" in sync
    assert "gtk_widget_set_size_request(home->page_strip, strip_width,\n\t\t    request_height);" in sync


def test_landscape_strip_reseeds_from_portrait_rows_into_side_columns():
    main = (HOME / "ctlsthome.c").read_text()
    helper = main[main.index("static void\nseed_strip_grid_anchors") : main.index(
        "\nstatic GtkWidget *\nmake_icon_page_panel"
    )]
    assert "home->landscape ? home_strip_rows(home) : home_cols(home)" in helper
    assert "home->landscape ? home_cols(home) : home_strip_rows(home)" in helper
    layout = main[main.index("static gboolean\nupdate_layout") : main.index(
        "\nstatic void\ndraw_analog_clock"
    )]
    assert layout.index("home->landscape = landscape") < layout.index(
        "seed_strip_grid_anchors(home)"
    ) < layout.index("render_launcher_page(home)")


def test_landscape_strip_reserves_its_transposed_share_of_page_width():
    main = (HOME / "ctlsthome.c").read_text()
    sync = main[main.index("static void\nsync_page_panel_widths") : main.index(
        "\nstatic void\nensure_page_panels"
    )]
    assert "(width - spacing) * home_strip_rows(home)" in sync
    assert "(home_cols(home) + home_strip_rows(home))" in sync
    assert "gtk_widget_set_size_request(home->strip_host, strip_w, -1)" in sync


def test_landscape_glance_card_uses_compact_vertical_padding():
    main = (HOME / "ctlsthome.c").read_text()
    assert (
        '".home-root.landscape .home-glance-card { padding: 4px 12px; }"'
        in main
    )


def test_rotation_keeps_live_content_without_blackout_or_cache():
    main = (HOME / "ctlsthome.c").read_text()
    for obsolete in ("HomeFrozenFrame", "home-rotation-cut-guard",
                     "HOME_ROTATION_COMPOSE_OPACITY", "rotation_transition_tick"):
        assert obsolete not in main
    assert "signal_rotation_ready(home)" in main  # old helper compatibility
    assert "width == home->layout_width" in main
    assert "height == home->layout_height" in main


def test_optional_pixel_transform_helper_does_not_black_out_home():
    path = ROOT / "scripts" / "set-output-transform"
    if not path.is_file():
        import pytest
        pytest.skip("Pixel rotation helper is outside the core source snapshot")
    transform = path.read_text()
    assert 'swaymsg output DSI-1 transform "$transform"' in transform
    assert "rotation_ready" not in transform
    assert "sleep" not in transform


def test_home_page_swipe_does_not_steal_vertical_shell_pulls():
    main = (HOME / "ctlsthome.c").read_text()
    start = main.index("static void\nlauncher_swiped")
    body = main[start : main.index("\nstatic void\nensure_launcher_button", start)]
    assert "fabs(velocity_x) <= fabs(velocity_y) * 1.15" in body
    assert body.index("fabs(velocity_x) <= fabs(velocity_y) * 1.15") < body.index(
        "home_pager_commit_page"
    )


def test_home_actions_ignore_motion_before_the_release():
    main = (HOME / "ctlsthome.c").read_text()
    assert "#define HOME_TAP_SLOP 12.0" in main
    widget_click = main[main.index("home_widget_clicked") : main.index(
        "static void\nkeyboard_control", main.index("home_widget_clicked")
    )]
    assert "item->tap_moved" in widget_click
    assert "hypot(offset_x, offset_y) >= HOME_TAP_SLOP" in main
    drag_end = main[main.index("static void\nhome_widget_drag_end") : main.index(
        "static void\nhome_widget_remove_clicked"
    )]
    assert "hypot(offset_x, offset_y) < HOME_TAP_SLOP" in drag_end


def test_page_edge_drag_does_not_treat_as_remove():
    main = (HOME / "ctlsthome.c").read_text()
    remove = main.index("static bool\nlauncher_point_is_remove")
    remove_body = main[remove : main.index("\npreferences_add_favorite", remove)]
    assert "page_edge_px" in remove_body
    assert "home screens" in remove_body
    assert "overlay_to_grid_point" not in remove_body
    edge = main.index("static gboolean\npage_edge_dwell_fire")
    edge_body = main[edge : main.index("\ncheck_drag_page_edge", edge)]
    assert "active_launcher_drag == NULL" in edge_body
    assert "relayout_launcher_icons(home)" in edge_body
    assert "set_launcher_page(home, new_page)" in edge_body
    assert "set_launcher_remove_armed(home, false)" in edge_body
    check = main.index("static void\ncheck_drag_page_edge")
    check_body = main[check : main.index("\napply_launcher_icon_move", check)]
    assert "Widgets stay on page 0" in check_body
    assert "active_drag_item" not in check_body
