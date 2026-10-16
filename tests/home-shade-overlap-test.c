/* Native geometry regression using the real daemon, no input or IPC writes. */
#define main ctlst_gesture_service_main
#include "../ctlst-gestured/ctlst-gestured.c"
#undef main
#include <assert.h>

int main(void)
{
	char directory[] = "/tmp/ctlst-shade-overlap-XXXXXX";
	char marker[256];
	struct daemon_state state = {0};
	const struct rect outputs[] = {{20, 30, 480, 960}, {20, 30, 960, 480}};
	assert(mkdtemp(directory));
	state.runtime_dir = directory;
	state.shade_home_pull_start_depth = SHADE_HOME_PULL_START_DEPTH;
	state.drawer_pull_bottom_inset = DRAWER_PULL_BOTTOM_INSET;
	strcpy(state.workspace_name, "1");
	snprintf(marker, sizeof(marker), "%s/ctlsthome.visible", directory);
	int fd = open(marker, O_CREAT | O_WRONLY, 0600);
	assert(fd >= 0);
	close(fd);
	assert(SHADE_HOME_PULL_START_DEPTH == 0.60);
	for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); i++) {
		state.output.rect = outputs[i];
		for (int percent = 45; percent <= 60; percent += 5) {
			struct point start = {.x = outputs[i].x + outputs[i].width / 2.,
			    .y = outputs[i].y + outputs[i].height * percent / 100.};
			struct point down = {.x = start.x, .y = start.y + 100};
			struct point up = {.x = start.x, .y = start.y - 100};
			struct point side = {.x = start.x + 100, .y = start.y};
			assert(shade_pull_geometry_matches(&state, start, down));
			assert(shade_pull_start_allowed(&state, start, down));
			assert(!shade_pull_geometry_matches(&state, start, up));
			assert(!shade_pull_geometry_matches(&state, start, side));
			assert(drawer_pull_geometry_matches(&state, start, up));
			assert(!drawer_pull_geometry_matches(&state, start, down));
			assert(!drawer_pull_geometry_matches(&state, start, side));
			strcpy(state.workspace_name, "app:9");
			assert(!shade_pull_geometry_matches(&state, start, down));
			strcpy(state.workspace_name, "1");
		}
		struct point below = {.x = outputs[i].x + outputs[i].width / 2.,
		    .y = outputs[i].y + outputs[i].height * .61};
		struct point down = {.x = below.x, .y = below.y + 100};
		assert(!shade_pull_geometry_matches(&state, below, down));
		state.shade_home_pull_start_depth = .40;
		below.y = outputs[i].y + outputs[i].height * .55;
		down.y = below.y + 100;
		assert(!shade_pull_geometry_matches(&state, below, down));
		state.shade_home_pull_start_depth = SHADE_HOME_PULL_START_DEPTH;
	}
	assert(unlink(marker) == 0);
	assert(rmdir(directory) == 0);
	puts("PASS: center overlap, direction, portrait/landscape, output offsets, app exclusion and configurable boundary");
	return 0;
}
