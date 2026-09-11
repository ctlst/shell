#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <inttypes.h>
#include <json-c/json.h>
#include <libinput.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define IPC_HEADER_SIZE 14
#define IPC_COMMAND 0
#define IPC_GET_WORKSPACES 1
#define IPC_GET_OUTPUTS 3
#define IPC_GET_TREE 4

#define FIRST_WORKSPACE 1
#define LAST_WORKSPACE 5
#define FIRST_APP_WORKSPACE 2
#define MAX_WORKSPACE_NAME 128
#define MAX_DYNAMIC_WORKSPACES 64
#define MAX_VISIBLE_DOCK_TASKS 3
#define SCRUB_BURST_US 250000
#define RESIZE_FRAME_US 16000
#define TAP_WINDOW_US 450000
#define BAR_REVEAL_US 3000000
#define SHELL_CLICK_BLOCK_US 750000
#define BOTTOM_PULL_ACTION 44
/*
 * Quick upward flick goes Home; slower pulls commit multitasking. Prefer
 * whole-gesture duration over instantaneous velocity — touch sample jitter on
 * the logical 480x960 output made peak px/s unusable (slow pulls looked fast,
 * fast flicks that took ~400ms looked slow).
 */
#define BOTTOM_PULL_HOME_FLICK_MAX_US 360000
/* Secondary: overall travel ≥ this many screen-heights per second. */
#define BOTTOM_PULL_HOME_FLICK_HEIGHTS_S 2.0
#define CONTROL_MESSAGE_SIZE 256
#define DROP_SIDE_MIN_WIDTH 58
#define DROP_SIDE_MAX_WIDTH 72
#define DROP_SIDE_MIN_HEIGHT 124
#define DROP_SIDE_MAX_HEIGHT 168
#define DROP_CHARM_SIDE_MARGIN 12
#define DROP_CHARM_TOP_MARGIN 12
#define DROP_CHARM_HEIGHT 58
#define DROP_CHARM_MIN_WIDTH 48
#define DROP_CHARM_MAX_WIDTH 70
#define DROP_CHARM_GAP 7
#define DROP_CHARM_NEW_WIDTH 52
#define DROP_CHARM_MAX_VISIBLE 5
#define DROP_CLOSE_WIDTH 160
#define DROP_CLOSE_HEIGHT 56
#define DROP_CLOSE_BOTTOM_MARGIN 18
#define SHELL_TOP_BAR_HEIGHT 44
#define SHELL_BOTTOM_BAR_HEIGHT 54
#define SHELL_LANDSCAPE_BOTTOM_BAR_HEIGHT 50
/* Match wvkbd-swaytouch -H / -L from keyboard-daemon. */
#define KEYBOARD_PORTRAIT_HEIGHT 340
#define KEYBOARD_LANDSCAPE_HEIGHT 190
#define ACTION_HUB_RIGHT 24
#define ACTION_HUB_RADIUS_X 28
#define ACTION_TARGET_RADIUS 52.0
#define ACTION_CANCEL_RADIUS 38.0
#define DRAWER_PULL_CENTER_MIN 0.10
#define DRAWER_PULL_CENTER_MAX 0.90
#define DRAWER_PULL_START_Y_MIN 0.50
#define DRAWER_PULL_BOTTOM_INSET 96.0
#define DRAWER_PULL_TRACKING_GAIN 1.0
#define DRAWER_PULL_COMMIT_DISTANCE 72.0
/* Leave a dead zone between Home paging and the upward drawer gesture. */
#define DRAWER_PULL_INTENT_DISTANCE 16.0
#define DRAWER_PULL_AXIS_RATIO 1.25
/* Full preview sooner so the pull does not feel like a long vertical slog. */
#define OVERVIEW_PULL_INTENT_FRACTION 0.10
#define OVERVIEW_PULL_FULL_FRACTION 0.45
#define OVERVIEW_PULL_COMMIT_FRACTION 0.22
#define OVERVIEW_PULL_INTENT_DISTANCE 16.0
/* Match ctlstshade's edge-to-edge surface for 1:1 pull tracking. */
#define SHADE_BOTTOM_MARGIN 0.0
#define SHADE_PULL_TRACKING_GAIN 1.0
#define SHADE_PULL_COMMIT_DISTANCE 96.0
#define SHADE_PULL_INTENT_DISTANCE 16.0
/* Home expands the top-edge shade target through the upper 40% of the output. */
#define SHADE_HOME_PULL_START_DEPTH 0.40
#define WORKSPACE_WHEEL_DETENT 36.0
/* Hold the home dash this long to commit the task-dock reveal animation. */
#define WORKSPACE_WHEEL_REVEAL_HOLD_US 200000
/* Wake the poll loop often enough to drive the slow hold inflate. */
#define WORKSPACE_WHEEL_INFLATE_FRAME_MS 16
/*
 * Hold inflate stays below the dock's interactive input threshold (0.82) until
 * the full E reveal commits at WORKSPACE_WHEEL_REVEAL_HOLD_US.
 */
#define WORKSPACE_WHEEL_INFLATE_MAX_PROGRESS 800
#define DOCK_TASK_PITCH 51.0
#define WORKSPACE_SELECTOR_WIDTH 210.0
#define WORKSPACE_WHEEL_LEFT_FRACTION 0.25
#define WORKSPACE_WHEEL_RIGHT_FRACTION 0.75
#define BACK_EDGE_WIDTH 24.0
#define BACK_SWIPE_DISTANCE 64.0
#define WINDOW_DRAG_HOLD_US 260000
#define TITLEBAR_SWIPE_MAX_US 320000
/* Decisive horizontal travel still transfers after a short hold. */
#define TITLEBAR_SWIPE_COMMIT_FRACTION 0.28
#define MAX_TOUCH_SLOTS 16
#define TOUCH_MOTION_SLOP 8.0
#define RIGHT_CLICK_HOLD_US 550000
#define FLOATING_TITLE_FORMAT "%title  ⤢"
#define TILED_TITLE_FORMAT "%title"

enum gesture_mode {
	GESTURE_NONE,
	GESTURE_WATCH,
	GESTURE_DRAG,
	GESTURE_TILED_DRAG,
	GESTURE_RESIZE,
	GESTURE_SNAPPED,
	GESTURE_DRAWER_PULL,
	GESTURE_OVERVIEW_PULL,
	GESTURE_SHADE_PULL,
	GESTURE_ACTION_ORBIT,
	GESTURE_WORKSPACE_WHEEL_PENDING,
	GESTURE_WORKSPACE_WHEEL,
	GESTURE_CONSUMED,
};

enum action_target {
	ACTION_NONE,
	ACTION_PAD,
	ACTION_KEYS,
	ACTION_CLOSE,
};

enum drop_target {
	DROP_NONE,
	DROP_WORKSPACE_LEFT,
	DROP_WORKSPACE_RIGHT,
	DROP_WORKSPACE_DIRECT,
	DROP_NEW_TASK,
	DROP_CLOSE,
};

struct rect {
	int x;
	int y;
	int width;
	int height;
};

struct point {
	double x;
	double y;
};

struct node {
	bool found;
	int64_t id;
	bool floating;
	int fullscreen;
	char app_id[160];
	struct rect rect;
	int deco_height;
	int window_width;
	int window_height;
	int workspace_windows;
	char workspace_name[MAX_WORKSPACE_NAME];
};

struct output {
	double scale;
	char transform[16];
	struct rect rect;
};

struct gesture_event {
	char phase;
	int fingers;
	double start_x;
	double start_y;
	double end_x;
	double end_y;
	int screen_width;
	int screen_height;
};

struct workspace_list {
	char home[MAX_WORKSPACE_NAME];
	char focused[MAX_WORKSPACE_NAME];
	char names[MAX_DYNAMIC_WORKSPACES][MAX_WORKSPACE_NAME];
	size_t count;
};

struct gesture_state {
	enum gesture_mode mode;
	struct point start;
	int64_t node_id;
	bool started_floating;
	bool allow_top;
	bool allow_bottom;
	enum drop_target drop_target;
	int drop_workspace_index;
	enum action_target action_target;
	bool action_engaged;
	int resize_start_x;
	int resize_start_y;
	int resize_width;
	int resize_height;
	int resize_max_width;
	int resize_max_height;
	int resize_move_x;
	int resize_move_top;
	int64_t last_resize_us;
	double wheel_anchor_x;
	struct workspace_list wheel_list;
	int wheel_index;
	bool wheel_dock_revealed;
	struct point last;
	int64_t last_us;
};

struct touch_slot {
	bool used;
	bool active;
	double start_x;
	double start_y;
	double x;
	double y;
};

struct touch_input {
	struct libinput *context;
	struct libinput_device *device;
	int fd;
	int active_count;
	int max_fingers;
	struct touch_slot slots[MAX_TOUCH_SLOTS];
};

struct daemon_state {
	const char *runtime_dir;
	const char *sway_socket;
	int command_fd;
	int control_fd;
	int lock_fd;
	int drawer_fd;
	int haptic_fd;
	int haptic_effect_id;
	char control_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char drawer_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char dock_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char shade_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char overview_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char drop_overlay_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char action_overlay_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char touch_state_path[256];
	char click_block_path[256];
	bool drawer_visible;
	bool shade_visible;
	bool overview_visible;
	bool dialer_visible;
	bool debug;
	double drawer_pull_bottom_inset;
	double drawer_pull_tracking_gain;
	double drawer_pull_commit_distance;
	double shade_pull_tracking_gain;
	double shade_pull_commit_distance;
	double shade_home_pull_start_depth;
	bool drawer_close_sequence;
	bool controls_revealed;
	int64_t controls_reveal_deadline;
	int64_t click_block_deadline;
	int workspace;
	char workspace_name[MAX_WORKSPACE_NAME];
	struct workspace_list workspace_cache;
	bool workspace_cache_valid;
	struct rect workspace_rect;
	struct output output;
	int64_t last_workspace_step;
	int64_t touch_down_us;
	int64_t tiled_hold_deadline;
	int64_t right_click_deadline;
	int64_t right_click_node_id;
	struct point right_click_point;
	bool right_click_fired;
	int64_t wheel_reveal_deadline;
	int64_t tap_node_id;
	int64_t tap_time;
	int tap_x;
	int tap_y;
	struct gesture_state gesture;
	struct touch_input input;
};

static volatile sig_atomic_t running = 1;

static void
handle_signal(int signal_number)
{
	(void)signal_number;
	running = 0;
}

static int64_t
monotonic_us(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

static double
configured_double(const char *name, double fallback, double minimum,
    double maximum)
{
	const char *value = getenv(name);
	char *end = NULL;
	double parsed;

	if (value == NULL || value[0] == '\0')
		return fallback;
	errno = 0;
	parsed = strtod(value, &end);
	if (errno != 0 || end == value || end[0] != '\0' || !isfinite(parsed) ||
	    parsed < minimum || parsed > maximum) {
		fprintf(stderr, "ctlst-gestured: ignoring invalid %s=%s\n", name,
		    value);
		return fallback;
	}
	return parsed;
}

static int write_full(int fd, const void *buffer, size_t size);
static void note_gesture_point(struct daemon_state *state, struct point point);
static void handle_down(struct daemon_state *state,
    const struct gesture_event *event);
static void handle_press(struct daemon_state *state,
    const struct gesture_event *event);
static void handle_release(struct daemon_state *state,
    const struct gesture_event *event);
static void cancel_right_click(struct daemon_state *state);
static bool refresh_workspace(struct daemon_state *state);
static bool dynamic_app_target(struct daemon_state *state, int direction,
    char *target, size_t target_size);
static bool move_window_to_new_task(struct daemon_state *state, int64_t node_id,
    bool focus, char *target, size_t target_size);
static int count_window_leaves(struct json_object *object);

static int
open_haptic_device(void)
{
	const char *configured = getenv("CTLST_HAPTIC_DEVICE");
	char name_path[96];
	char event_path[32];
	char name[80];
	int fd;

	if (configured != NULL && configured[0] != '\0')
		return open(configured, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	for (int index = 0; index < 32; index++) {
		ssize_t length;

		snprintf(name_path, sizeof(name_path),
		    "/sys/class/input/event%d/device/name", index);
		fd = open(name_path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		length = read(fd, name, sizeof(name) - 1);
		close(fd);
		if (length <= 0)
			continue;
		name[length] = '\0';
		name[strcspn(name, "\r\n")] = '\0';
		if (strcmp(name, "drv2624:haptics") != 0)
			continue;
		snprintf(event_path, sizeof(event_path), "/dev/input/event%d", index);
		return open(event_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	}
	return -1;
}

static void
init_haptic(struct daemon_state *state)
{
	struct ff_effect effect = {0};

	state->haptic_fd = open_haptic_device();
	state->haptic_effect_id = -1;
	if (state->haptic_fd < 0)
		return;
	effect.type = FF_RUMBLE;
	effect.id = -1;
	effect.u.rumble.strong_magnitude = 0x7fff;
	effect.replay.length = 15;
	if (ioctl(state->haptic_fd, EVIOCSFF, &effect) < 0) {
		close(state->haptic_fd);
		state->haptic_fd = -1;
		return;
	}
	state->haptic_effect_id = effect.id;
}

static void
pulse_workspace_haptic(const struct daemon_state *state)
{
	struct input_event event = {
		.type = EV_FF,
		.code = (uint16_t)state->haptic_effect_id,
		.value = 1,
	};

	if (state->haptic_fd >= 0 && state->haptic_effect_id >= 0) {
		ssize_t written = write(state->haptic_fd, &event, sizeof(event));

		if (written != (ssize_t)sizeof(event))
			return;
	}
}

static void
block_shell_clicks(struct daemon_state *state)
{
	char record[32];
	int length;
	int fd;

	length = snprintf(record, sizeof(record), "%" PRId64 "\n",
	    monotonic_us() + SHELL_CLICK_BLOCK_US);
	if (length <= 0 || length >= (int)sizeof(record))
		return;
	fd = open(state->click_block_path,
	    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		return;
	(void)write_full(fd, record, (size_t)length);
	close(fd);
	state->click_block_deadline =
	    monotonic_us() + SHELL_CLICK_BLOCK_US;
}

static void
record_touch_release(struct daemon_state *state, struct point start,
    struct point end)
{
	char record[64];
	double dx = end.x - start.x;
	double dy = end.y - start.y;
	bool moved = hypot(dx, dy) > 35.0;
	int length;
	int fd;

	length = snprintf(record, sizeof(record), "%" PRId64 " %d\n",
	    monotonic_us(), moved ? 1 : 0);
	if (moved) {
		block_shell_clicks(state);
	} else {
		unlink(state->click_block_path);
		state->click_block_deadline = 0;
	}
	if (length <= 0 || length >= (int)sizeof(record))
		return;
	fd = open(state->touch_state_path,
	    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		return;
	(void)write_full(fd, record, (size_t)length);
	close(fd);
}

static int
write_full(int fd, const void *buffer, size_t size)
{
	const char *cursor = buffer;

	while (size > 0) {
		ssize_t written = write(fd, cursor, size);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		cursor += written;
		size -= (size_t)written;
	}
	return 0;
}

static int
read_full(int fd, void *buffer, size_t size)
{
	char *cursor = buffer;

	while (size > 0) {
		ssize_t received = read(fd, cursor, size);
		if (received == 0)
			return -1;
		if (received < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		cursor += received;
		size -= (size_t)received;
	}
	return 0;
}

static int
ipc_connect(const char *path)
{
	struct sockaddr_un address = {0};
	struct timeval timeout = {
		.tv_sec = 0,
		.tv_usec = 75000,
	};
	int fd;

	if (strlen(path) >= sizeof(address.sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
	    sizeof(timeout)) < 0 ||
	    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
	    sizeof(timeout)) < 0) {
		close(fd);
		return -1;
	}

	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, path);
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int
ipc_send(int fd, uint32_t type, const char *payload)
{
	unsigned char header[IPC_HEADER_SIZE] = "i3-ipc";
	uint32_t size = (uint32_t)strlen(payload);

	memcpy(header + 6, &size, sizeof(size));
	memcpy(header + 10, &type, sizeof(type));
	if (write_full(fd, header, sizeof(header)) < 0)
		return -1;
	return write_full(fd, payload, size);
}

static char *
ipc_receive(int fd, uint32_t *type)
{
	unsigned char header[IPC_HEADER_SIZE];
	uint32_t size;
	char *payload;

	if (read_full(fd, header, sizeof(header)) < 0)
		return NULL;
	if (memcmp(header, "i3-ipc", 6) != 0) {
		errno = EPROTO;
		return NULL;
	}

	memcpy(&size, header + 6, sizeof(size));
	memcpy(type, header + 10, sizeof(*type));
	if (size > 16 * 1024 * 1024) {
		errno = EOVERFLOW;
		return NULL;
	}

	payload = malloc((size_t)size + 1);
	if (payload == NULL)
		return NULL;
	if (read_full(fd, payload, size) < 0) {
		free(payload);
		return NULL;
	}
	payload[size] = '\0';
	return payload;
}

static char *
ipc_request(struct daemon_state *state, uint32_t request_type,
    const char *request, uint32_t *response_type)
{
	char *response;
	int attempt;

	for (attempt = 0; attempt < 2; attempt++) {
		if (state->command_fd < 0)
			state->command_fd = ipc_connect(state->sway_socket);
		if (state->command_fd >= 0 &&
		    ipc_send(state->command_fd, request_type, request) == 0) {
			response = ipc_receive(state->command_fd, response_type);
			if (response != NULL)
				return response;
		}
		if (state->command_fd >= 0)
			close(state->command_fd);
		state->command_fd = -1;
	}
	return NULL;
}

static bool
command_succeeded(const char *payload)
{
	struct json_object *root;
	struct json_object *result;
	struct json_object *success;
	bool succeeded = false;

	root = json_tokener_parse(payload);
	if (root == NULL)
		return false;
	if (json_object_is_type(root, json_type_array) &&
	    json_object_array_length(root) > 0) {
		result = json_object_array_get_idx(root, 0);
		if (json_object_object_get_ex(result, "success", &success))
			succeeded = json_object_get_boolean(success);
	}
	json_object_put(root);
	return succeeded;
}

static bool
run_command(struct daemon_state *state, const char *command)
{
	uint32_t type;
	char *response;
	bool succeeded;

	response = ipc_request(state, IPC_COMMAND, command, &type);
	if (response == NULL)
		return false;
	succeeded = type == IPC_COMMAND && command_succeeded(response);
	free(response);
	return succeeded;
}

static int
json_int(struct json_object *object, const char *name, int fallback)
{
	struct json_object *value;

	if (!json_object_object_get_ex(object, name, &value))
		return fallback;
	return json_object_get_int(value);
}

static int64_t
json_int64(struct json_object *object, const char *name, int64_t fallback)
{
	struct json_object *value;

	if (!json_object_object_get_ex(object, name, &value))
		return fallback;
	return json_object_get_int64(value);
}

static double
json_double(struct json_object *object, const char *name, double fallback)
{
	struct json_object *value;

	if (!json_object_object_get_ex(object, name, &value))
		return fallback;
	return json_object_get_double(value);
}

static const char *
json_string(struct json_object *object, const char *name, const char *fallback)
{
	struct json_object *value;

	if (!json_object_object_get_ex(object, name, &value) ||
	    json_object_is_type(value, json_type_null))
		return fallback;
	return json_object_get_string(value);
}

static bool
object_bool(struct json_object *object, const char *name)
{
	struct json_object *value;

	return json_object_object_get_ex(object, name, &value) &&
	    json_object_get_boolean(value);
}

static bool
safe_workspace_name(const char *name)
{
	const char *cursor;

	if (name[0] >= '0' && name[0] <= '9') {
		for (cursor = name; *cursor != '\0'; cursor++) {
			if (*cursor < '0' || *cursor > '9')
				return false;
		}
		return true;
	}
	if (strncmp(name, "app:", 4) != 0 || name[4] == '\0')
		return false;
	for (cursor = name + 4; *cursor != '\0'; cursor++) {
		if (*cursor < '0' || *cursor > '9')
			return false;
	}
	return true;
}

static int
numeric_workspace_name(const char *name)
{
	if (!safe_workspace_name(name) ||
	    strncmp(name, "app:", 4) == 0)
		return 0;
	return atoi(name);
}

static bool
workspace_list_add(struct workspace_list *list, const char *name)
{
	if (list->count >= MAX_DYNAMIC_WORKSPACES ||
	    !safe_workspace_name(name))
		return false;
	for (size_t index = 0; index < list->count; index++) {
		if (strcmp(list->names[index], name) == 0)
			return true;
	}
	snprintf(list->names[list->count], MAX_WORKSPACE_NAME, "%s", name);
	list->count++;
	return true;
}

static bool
workspace_manager_list(const struct daemon_state *state,
    struct workspace_list *list)
{
	struct sockaddr_un address;
	struct json_object *root;
	struct json_object *tasks;
	char request[] = "{\"command\":\"list\"}\n";
	char response[65536];
	const char *home;
	const char *focused;
	ssize_t received;
	size_t offset = 0;
	int fd;
	struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};

	memset(list, 0, sizeof(*list));
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return false;
	(void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
	    sizeof(timeout));
	(void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
	    sizeof(timeout));
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	if (snprintf(address.sun_path, sizeof(address.sun_path), "%s/%s",
	    state->runtime_dir, "ctlst-workspaces.sock") >=
	    (int)sizeof(address.sun_path) ||
	    connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    write_full(fd, request, strlen(request)) < 0) {
		close(fd);
		return false;
	}
	while (offset + 1 < sizeof(response)) {
		received = read(fd, response + offset,
		    sizeof(response) - offset - 1);
		if (received <= 0)
			break;
		offset += (size_t)received;
		if (memchr(response, '\n', offset) != NULL)
			break;
	}
	close(fd);
	if (offset == 0)
		return false;
	response[offset] = '\0';
	root = json_tokener_parse(response);
	if (root == NULL || !object_bool(root, "ok")) {
		if (root != NULL)
			json_object_put(root);
		return false;
	}
	home = json_string(root, "home", "");
	focused = json_string(root, "focused", "");
	if (!safe_workspace_name(home) || !safe_workspace_name(focused)) {
		json_object_put(root);
		return false;
	}
	snprintf(list->home, sizeof(list->home), "%s", home);
	snprintf(list->focused, sizeof(list->focused), "%s", focused);
	if (!workspace_list_add(list, list->home) ||
	    !json_object_object_get_ex(root, "tasks", &tasks) ||
	    !json_object_is_type(tasks, json_type_array)) {
		json_object_put(root);
		return false;
	}
	for (size_t index = 0; index < json_object_array_length(tasks); index++) {
		struct json_object *task = json_object_array_get_idx(tasks, index);
		const char *name = json_string(task, "workspace", "");

		if (!workspace_list_add(list, name)) {
			json_object_put(root);
			return false;
		}
	}
	json_object_put(root);
	return list->count > 0;
}

static bool
sway_workspace_list(struct daemon_state *state, struct workspace_list *list)
{
	uint32_t type;
	char *payload;
	struct json_object *root;
	size_t index;

	memset(list, 0, sizeof(*list));
	payload = ipc_request(state, IPC_GET_WORKSPACES, "", &type);
	if (payload == NULL)
		return false;
	root = type == IPC_GET_WORKSPACES ? json_tokener_parse(payload) : NULL;
	free(payload);
	if (root == NULL || !json_object_is_type(root, json_type_array)) {
		if (root != NULL)
			json_object_put(root);
		return false;
	}
	snprintf(list->home, sizeof(list->home), "%d", FIRST_WORKSPACE);
	if (!workspace_list_add(list, list->home)) {
		json_object_put(root);
		return false;
	}
	for (index = 0; index < json_object_array_length(root); index++) {
		struct json_object *workspace =
		    json_object_array_get_idx(root, index);
		const char *name = json_string(workspace, "name", "");

		if (!workspace_list_add(list, name)) {
			json_object_put(root);
			return false;
		}
		if (object_bool(workspace, "focused"))
			snprintf(list->focused, sizeof(list->focused), "%s",
			    name);
	}
	json_object_put(root);
	if (list->focused[0] == '\0')
		snprintf(list->focused, sizeof(list->focused), "%s",
		    state->workspace_name);
	return list->count > 0 && safe_workspace_name(list->focused);
}

static bool
refresh_workspace_cache(struct daemon_state *state)
{
	struct workspace_list list;

	if (!workspace_manager_list(state, &list) &&
	    !sway_workspace_list(state, &list))
		return false;
	state->workspace_cache = list;
	state->workspace_cache_valid = true;
	if (state->gesture.mode != GESTURE_WORKSPACE_WHEEL_PENDING &&
	    state->gesture.mode != GESTURE_WORKSPACE_WHEEL) {
		snprintf(state->workspace_name, sizeof(state->workspace_name),
		    "%s", list.focused);
		state->workspace = numeric_workspace_name(list.focused);
	}
	return true;
}

static const struct workspace_list *
cached_workspace_list(struct daemon_state *state)
{
	if (!state->workspace_cache_valid && !refresh_workspace_cache(state))
		return NULL;
	return &state->workspace_cache;
}

static bool
workspace_manager_move(const struct daemon_state *state, int64_t con_id,
    const char *workspace, bool focus, char *target, size_t target_size)
{
	struct sockaddr_un address;
	struct json_object *root;
	char request[256];
	char response[1024];
	const char *moved_workspace;
	ssize_t received;
	size_t offset = 0;
	int fd;
	struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};

	if (strcmp(workspace, "new") != 0 && !safe_workspace_name(workspace))
		return false;
	if (snprintf(request, sizeof(request),
	    "{\"command\":\"move\",\"con_id\":%" PRId64
	    ",\"workspace\":\"%s\",\"focus\":%s}\n",
	    con_id, workspace, focus ? "true" : "false") >=
	    (int)sizeof(request))
		return false;
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return false;
	(void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
	    sizeof(timeout));
	(void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
	    sizeof(timeout));
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	if (snprintf(address.sun_path, sizeof(address.sun_path), "%s/%s",
	    state->runtime_dir, "ctlst-workspaces.sock") >=
	    (int)sizeof(address.sun_path) ||
	    connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    write_full(fd, request, strlen(request)) < 0) {
		close(fd);
		return false;
	}
	while (offset + 1 < sizeof(response)) {
		received = read(fd, response + offset,
		    sizeof(response) - offset - 1);
		if (received <= 0)
			break;
		offset += (size_t)received;
		if (memchr(response, '\n', offset) != NULL)
			break;
	}
	close(fd);
	if (offset == 0)
		return false;
	response[offset] = '\0';
	root = json_tokener_parse(response);
	if (root == NULL || !object_bool(root, "ok")) {
		if (root != NULL)
			json_object_put(root);
		return false;
	}
	moved_workspace = json_string(root, "workspace", "");
	if (!safe_workspace_name(moved_workspace) ||
	    strcmp(moved_workspace, "1") == 0) {
		json_object_put(root);
		return false;
	}
	snprintf(target, target_size, "%s", moved_workspace);
	json_object_put(root);
	return true;
}

static int
workspace_list_index(const struct workspace_list *list, const char *name)
{
	for (size_t index = 0; index < list->count; index++) {
		if (strcmp(list->names[index], name) == 0)
			return (int)index;
	}
	return -1;
}

static struct rect
json_rect(struct json_object *object, const char *name)
{
	struct json_object *value;
	struct rect result = {0};

	if (!json_object_object_get_ex(object, name, &value))
		return result;
	result.x = json_int(value, "x", 0);
	result.y = json_int(value, "y", 0);
	result.width = json_int(value, "width", 0);
	result.height = json_int(value, "height", 0);
	return result;
}

static void
node_from_json(struct json_object *object, struct node *node)
{
	const char *floating;
	struct json_object *properties;

	memset(node, 0, sizeof(*node));
	node->found = true;
	node->id = json_int64(object, "id", 0);
	floating = json_string(object, "floating", "");
	node->floating = strcmp(floating, "auto_on") == 0 ||
	    strcmp(floating, "user_on") == 0;
	node->fullscreen = json_int(object, "fullscreen_mode", 0);
	snprintf(node->app_id, sizeof(node->app_id), "%s",
	    json_string(object, "app_id", ""));
	if (node->app_id[0] == '\0' &&
	    json_object_object_get_ex(object, "window_properties", &properties))
		snprintf(node->app_id, sizeof(node->app_id), "%s",
		    json_string(properties, "class", ""));
	node->rect = json_rect(object, "rect");
	node->deco_height = json_rect(object, "deco_rect").height;
	node->window_width = json_rect(object, "window_rect").width;
	node->window_height = json_rect(object, "window_rect").height;
	if (node->window_width <= 0)
		node->window_width = node->rect.width;
	if (node->window_height <= 0)
		node->window_height = node->rect.height;
}

static bool
find_focused_node(struct json_object *object, struct node *node)
{
	static const char *children_names[] = {"nodes", "floating_nodes"};
	struct json_object *children;
	size_t group;
	size_t index;

	for (group = 0; group < sizeof(children_names) / sizeof(children_names[0]);
	    group++) {
		if (!json_object_object_get_ex(object, children_names[group],
		    &children) ||
		    !json_object_is_type(children, json_type_array))
			continue;
		for (index = 0; index < json_object_array_length(children);
		    index++) {
			if (find_focused_node(
			    json_object_array_get_idx(children, index), node))
				return true;
		}
	}

	if (object_bool(object, "focused")) {
		const char *type = json_string(object, "type", "");
		if (strcmp(type, "con") == 0 ||
		    strcmp(type, "floating_con") == 0) {
			node_from_json(object, node);
			return true;
		}
	}
	return false;
}

static bool
find_node_by_id(struct json_object *object, int64_t id, struct node *node)
{
	static const char *children_names[] = {"nodes", "floating_nodes"};
	struct json_object *children;
	size_t group;
	size_t index;

	if (json_int64(object, "id", 0) == id) {
		node_from_json(object, node);
		return true;
	}

	for (group = 0; group < sizeof(children_names) / sizeof(children_names[0]);
	    group++) {
		if (!json_object_object_get_ex(object, children_names[group],
		    &children) ||
		    !json_object_is_type(children, json_type_array))
			continue;
		for (index = 0; index < json_object_array_length(children);
		    index++) {
			if (find_node_by_id(
			    json_object_array_get_idx(children, index), id, node))
				return true;
		}
	}
	return false;
}

static struct json_object *
find_object_by_id(struct json_object *object, int64_t id)
{
	static const char *children_names[] = {"nodes", "floating_nodes"};
	struct json_object *children;

	if (json_int64(object, "id", 0) == id)
		return object;
	for (size_t group = 0;
	    group < sizeof(children_names) / sizeof(children_names[0]); group++) {
		if (!json_object_object_get_ex(object, children_names[group],
		    &children) || !json_object_is_type(children, json_type_array))
			continue;
		for (size_t index = 0;
		    index < json_object_array_length(children); index++) {
			struct json_object *found = find_object_by_id(
			    json_object_array_get_idx(children, index), id);

			if (found != NULL)
				return found;
		}
	}
	return NULL;
}

static bool
find_focus_chain_leaf(struct json_object *root, struct json_object *object,
    struct node *node)
{
	static const char *children_names[] = {"nodes", "floating_nodes"};
	struct json_object *focus;
	const char *type = json_string(object, "type", "");
	bool has_children = false;

	for (size_t group = 0;
	    group < sizeof(children_names) / sizeof(children_names[0]); group++) {
		struct json_object *children;

		if (json_object_object_get_ex(object, children_names[group],
		    &children) && json_object_is_type(children, json_type_array) &&
		    json_object_array_length(children) > 0)
			has_children = true;
	}
	if (!has_children && (strcmp(type, "con") == 0 ||
	    strcmp(type, "floating_con") == 0)) {
		node_from_json(object, node);
		return true;
	}
	if (!json_object_object_get_ex(object, "focus", &focus) ||
	    !json_object_is_type(focus, json_type_array))
		return false;
	for (size_t index = 0; index < json_object_array_length(focus); index++) {
		int64_t id = json_object_get_int64(
		    json_object_array_get_idx(focus, index));
		struct json_object *child = find_object_by_id(root, id);

		if (child != NULL && find_focus_chain_leaf(root, child, node))
			return true;
	}
	return false;
}

static bool
find_focused_workspace_leaf(struct json_object *root,
    struct json_object *object, struct node *node)
{
	static const char *children_names[] = {"nodes", "floating_nodes"};
	struct json_object *children;

	if (strcmp(json_string(object, "type", ""), "workspace") == 0 &&
	    object_bool(object, "focused"))
		return find_focus_chain_leaf(root, object, node);
	for (size_t group = 0;
	    group < sizeof(children_names) / sizeof(children_names[0]); group++) {
		if (!json_object_object_get_ex(object, children_names[group],
		    &children) || !json_object_is_type(children, json_type_array))
			continue;
		for (size_t index = 0;
		    index < json_object_array_length(children); index++) {
			if (find_focused_workspace_leaf(root,
			    json_object_array_get_idx(children, index), node))
				return true;
		}
	}
	return false;
}

static bool
find_node_workspace_context(struct json_object *object, int64_t node_id,
    struct node *node)
{
	static const char *children_names[] = {"nodes", "floating_nodes"};
	struct json_object *children;

	if (strcmp(json_string(object, "type", ""), "workspace") == 0 &&
	    find_object_by_id(object, node_id) != NULL) {
		snprintf(node->workspace_name, sizeof(node->workspace_name), "%s",
		    json_string(object, "name", ""));
		node->workspace_windows = count_window_leaves(object);
		return true;
	}
	for (size_t group = 0;
	    group < sizeof(children_names) / sizeof(children_names[0]); group++) {
		if (!json_object_object_get_ex(object, children_names[group],
		    &children) || !json_object_is_type(children, json_type_array))
			continue;
		for (size_t index = 0;
		    index < json_object_array_length(children); index++) {
			if (find_node_workspace_context(
			    json_object_array_get_idx(children, index), node_id,
			    node))
				return true;
		}
	}
	return false;
}

static bool
load_tree_node(struct daemon_state *state, int64_t id, struct node *node)
{
	uint32_t type;
	char *payload;
	struct json_object *root;
	bool found;

	payload = ipc_request(state, IPC_GET_TREE, "", &type);
	if (payload == NULL)
		return false;
	root = type == IPC_GET_TREE ? json_tokener_parse(payload) : NULL;
	free(payload);
	if (root == NULL)
		return false;
	if (id == 0) {
		found = find_focused_node(root, node);
		if (!found)
			found = find_focused_workspace_leaf(root, root, node);
	} else {
		found = find_node_by_id(root, id, node);
	}
	if (found)
		(void)find_node_workspace_context(root, node->id, node);
	json_object_put(root);
	return found;
}

static int
count_window_leaves(struct json_object *object)
{
	static const char *children_names[] = {"nodes", "floating_nodes"};
	struct json_object *children;
	int count = 0;

	for (size_t group = 0;
	    group < sizeof(children_names) / sizeof(children_names[0]); group++) {
		if (!json_object_object_get_ex(object, children_names[group],
		    &children) || !json_object_is_type(children, json_type_array))
			continue;
		for (size_t index = 0;
		    index < json_object_array_length(children); index++) {
			count += count_window_leaves(
			    json_object_array_get_idx(children, index));
		}
	}
	if (count > 0)
		return count;
	if (strcmp(json_string(object, "type", ""), "con") != 0 &&
	    strcmp(json_string(object, "type", ""), "floating_con") != 0)
		return 0;
	return json_string(object, "app_id", "")[0] != '\0' ||
	    json_int64(object, "pid", 0) > 0 ? 1 : 0;
}

static int
find_workspace_window_count(struct json_object *object, const char *workspace)
{
	static const char *children_names[] = {"nodes", "floating_nodes"};
	struct json_object *children;

	if (strcmp(json_string(object, "type", ""), "workspace") == 0 &&
	    strcmp(json_string(object, "name", ""), workspace) == 0)
		return count_window_leaves(object);
	for (size_t group = 0;
	    group < sizeof(children_names) / sizeof(children_names[0]); group++) {
		if (!json_object_object_get_ex(object, children_names[group],
		    &children) || !json_object_is_type(children, json_type_array))
			continue;
		for (size_t index = 0;
		    index < json_object_array_length(children); index++) {
			int count = find_workspace_window_count(
				json_object_array_get_idx(children, index), workspace);

			if (count >= 0)
				return count;
		}
	}
	return -1;
}

static int
focused_workspace_window_count(struct daemon_state *state)
{
	uint32_t type;
	char *payload;
	struct json_object *root;
	int count;

	if (!refresh_workspace(state))
		return -1;
	payload = ipc_request(state, IPC_GET_TREE, "", &type);
	if (payload == NULL)
		return -1;
	root = type == IPC_GET_TREE ? json_tokener_parse(payload) : NULL;
	free(payload);
	if (root == NULL)
		return -1;
	count = find_workspace_window_count(root, state->workspace_name);
	json_object_put(root);
	return count;
}

static bool
refresh_workspace(struct daemon_state *state)
{
	uint32_t type;
	char *payload;
	struct json_object *root;
	size_t index;
	bool found = false;

	payload = ipc_request(state, IPC_GET_WORKSPACES, "", &type);
	if (payload == NULL)
		return false;
	root = type == IPC_GET_WORKSPACES ? json_tokener_parse(payload) : NULL;
	free(payload);
	if (root == NULL || !json_object_is_type(root, json_type_array)) {
		if (root != NULL)
			json_object_put(root);
		return false;
	}

	for (index = 0; index < json_object_array_length(root); index++) {
		struct json_object *workspace =
		    json_object_array_get_idx(root, index);
		if (!object_bool(workspace, "focused"))
			continue;
		snprintf(state->workspace_name, sizeof(state->workspace_name), "%s",
		    json_string(workspace, "name", "1"));
		state->workspace = json_int(workspace, "num", 0);
		if (!safe_workspace_name(state->workspace_name))
			snprintf(state->workspace_name, sizeof(state->workspace_name), "1");
		if (state->workspace == 0 && strcmp(state->workspace_name, "1") == 0)
			state->workspace = FIRST_WORKSPACE;
		state->workspace_rect = json_rect(workspace, "rect");
		found = true;
		break;
	}
	json_object_put(root);
	return found;
}

static bool
refresh_output(struct daemon_state *state)
{
	uint32_t type;
	char *payload;
	struct json_object *root;
	const char *preferred_name;
	size_t index;
	bool found = false;

	payload = ipc_request(state, IPC_GET_OUTPUTS, "", &type);
	if (payload == NULL)
		return false;
	root = type == IPC_GET_OUTPUTS ? json_tokener_parse(payload) : NULL;
	free(payload);
	if (root == NULL || !json_object_is_type(root, json_type_array)) {
		if (root != NULL)
			json_object_put(root);
		return false;
	}

	preferred_name = getenv("CTLST_OUTPUT_NAME");
	if (preferred_name == NULL || preferred_name[0] == '\0')
		preferred_name = "DSI-1";
	for (index = 0; index < json_object_array_length(root); index++) {
		struct json_object *output = json_object_array_get_idx(root, index);
		if (strcmp(json_string(output, "name", ""), preferred_name) != 0)
			continue;
		state->output.scale = json_double(output, "scale", 1.0);
		if (state->output.scale <= 0.0)
			state->output.scale = 1.0;
		snprintf(state->output.transform,
		    sizeof(state->output.transform), "%s",
		    json_string(output, "transform", "normal"));
		state->output.rect = json_rect(output, "rect");
		found = true;
		break;
	}
	for (index = 0; !found &&
	    index < json_object_array_length(root); index++) {
		struct json_object *output = json_object_array_get_idx(root, index);

		if (!object_bool(output, "active"))
			continue;
		state->output.scale = json_double(output, "scale", 1.0);
		if (state->output.scale <= 0.0)
			state->output.scale = 1.0;
		snprintf(state->output.transform,
		    sizeof(state->output.transform), "%s",
		    json_string(output, "transform", "normal"));
		state->output.rect = json_rect(output, "rect");
		found = true;
	}
	json_object_put(root);
	return found;
}

static int
workspace_target(int workspace, int direction)
{
	int target = workspace + direction;

	if (target > LAST_WORKSPACE)
		return FIRST_WORKSPACE;
	if (target < FIRST_WORKSPACE)
		return LAST_WORKSPACE;
	return target;
}

static int
app_workspace_target(int workspace, int direction)
{
	int target;

	if (workspace < FIRST_APP_WORKSPACE || workspace > LAST_WORKSPACE)
		return direction > 0 ? FIRST_APP_WORKSPACE : LAST_WORKSPACE;
	target = workspace + direction;
	if (target > LAST_WORKSPACE)
		return FIRST_APP_WORKSPACE;
	if (target < FIRST_APP_WORKSPACE)
		return LAST_WORKSPACE;
	return target;
}

static bool
step_workspace(struct daemon_state *state, int direction, bool move)
{
	char command[256];
	const struct workspace_list *list;
	const char *current;
	const char *target_name;
	int index;
	int target;
	int64_t now = monotonic_us();

	list = cached_workspace_list(state);
	if (list != NULL) {
		current = state->last_workspace_step != 0 &&
		    now - state->last_workspace_step <= SCRUB_BURST_US &&
		    safe_workspace_name(state->workspace_name) ?
		    state->workspace_name : list->focused;
		index = workspace_list_index(list, current);
		if (move) {
			/* Moving a window never targets Home. */
			if (strcmp(current, list->home) == 0) {
				if (list->count <= 1)
					return false;
				index = direction > 0 ? 1 : (int)list->count - 1;
			} else {
				if (index < 1)
					index = direction > 0 ? 1 : (int)list->count - 1;
				index += direction > 0 ? 1 : -1;
				if (index >= (int)list->count)
					index = 1;
				if (index < 1)
					index = (int)list->count - 1;
			}
		} else {
			if (index < 0)
				index = 0;
			index = (index + (direction > 0 ? 1 : -1) +
			    (int)list->count) % (int)list->count;
		}
		target_name = list->names[index];
		if (move) {
			snprintf(command, sizeof(command),
			    "move container to workspace %.*s; workspace %.*s",
			    48, target_name, 48, target_name);
		} else {
			snprintf(command, sizeof(command), "workspace %.*s", 48,
			    target_name);
		}
		if (!run_command(state, command))
			return false;
		snprintf(state->workspace_name, sizeof(state->workspace_name),
		    "%.*s", 48, target_name);
		snprintf(state->workspace_cache.focused,
		    sizeof(state->workspace_cache.focused), "%.*s", 48,
		    target_name);
		state->workspace = numeric_workspace_name(target_name);
		state->last_workspace_step = now;
		return true;
	}

	if (state->last_workspace_step == 0 ||
	    now - state->last_workspace_step > SCRUB_BURST_US) {
		if (!refresh_workspace(state))
			return false;
	}
	target = move ? app_workspace_target(state->workspace, direction) :
	    workspace_target(state->workspace, direction);
	if (move) {
		snprintf(command, sizeof(command),
		    "move container to workspace number %d; workspace number %d",
		    target, target);
	} else {
		snprintf(command, sizeof(command), "workspace number %d", target);
	}
	if (!run_command(state, command))
		return false;
	state->workspace = target;
	snprintf(state->workspace_name, sizeof(state->workspace_name), "%d",
	    target);
	state->last_workspace_step = now;
	return true;
}

static bool __attribute__((unused))
select_workspace_name(struct daemon_state *state, const char *name, bool move)
{
	char command[256];

	if (!safe_workspace_name(name) || (move && strcmp(name, "1") == 0))
		return false;
	if (move) {
		snprintf(command, sizeof(command),
		    "move container to workspace %.*s; workspace %.*s",
		    48, name, 48, name);
	} else {
		snprintf(command, sizeof(command), "workspace %.*s", 48, name);
	}
	if (!run_command(state, command))
		return false;
	snprintf(state->workspace_name, sizeof(state->workspace_name), "%.*s",
	    48, name);
	state->workspace = strcmp(name, "1") == 0 ? FIRST_WORKSPACE : 0;
	state->last_workspace_step = monotonic_us();
	return true;
}

static bool
select_workspace(struct daemon_state *state, int target, bool move)
{
	char command[160];

	if (target < FIRST_WORKSPACE || target > LAST_WORKSPACE)
		return false;
	if (move && (target < FIRST_APP_WORKSPACE ||
	    target > LAST_WORKSPACE))
		return false;

	if (move) {
		snprintf(command, sizeof(command),
		    "move container to workspace number %d; workspace number %d",
		    target, target);
	} else {
		snprintf(command, sizeof(command), "workspace number %d", target);
	}
	if (!run_command(state, command))
		return false;
	state->workspace = target;
	snprintf(state->workspace_name, sizeof(state->workspace_name), "%d",
	    target);
	state->last_workspace_step = monotonic_us();
	return true;
}

static struct point
transform_point(const struct daemon_state *state, double raw_x, double raw_y,
    int raw_width, int raw_height)
{
	struct point point;
	double scale = state->output.scale;

	if (strcmp(state->output.transform, "90") == 0) {
		point.x = raw_y / scale;
		point.y = (raw_width - raw_x) / scale;
	} else if (strcmp(state->output.transform, "180") == 0) {
		point.x = (raw_width - raw_x) / scale;
		point.y = (raw_height - raw_y) / scale;
	} else if (strcmp(state->output.transform, "270") == 0) {
		point.x = (raw_height - raw_y) / scale;
		point.y = raw_x / scale;
	} else {
		point.x = raw_x / scale;
		point.y = raw_y / scale;
	}
	point.x += state->output.rect.x;
	point.y += state->output.rect.y;
	return point;
}

static int
input_open_restricted(const char *path, int flags, void *data)
{
	int fd;

	(void)data;
	fd = open(path, flags | O_CLOEXEC);
	return fd < 0 ? -errno : fd;
}

static void
input_close_restricted(int fd, void *data)
{
	(void)data;
	close(fd);
}

static const struct libinput_interface input_interface = {
	.open_restricted = input_open_restricted,
	.close_restricted = input_close_restricted,
};

static void
raw_output_size(const struct daemon_state *state, int *width, int *height)
{
	double scale = state->output.scale > 0 ? state->output.scale : 1.0;
	bool rotated = strcmp(state->output.transform, "90") == 0 ||
	    strcmp(state->output.transform, "270") == 0;

	if (rotated) {
		*width = (int)lround(state->output.rect.height * scale);
		*height = (int)lround(state->output.rect.width * scale);
	} else {
		*width = (int)lround(state->output.rect.width * scale);
		*height = (int)lround(state->output.rect.height * scale);
	}
	if (*width < 1)
		*width = 1;
	if (*height < 1)
		*height = 1;
}

static void
reset_touch_input(struct touch_input *input)
{
	memset(input->slots, 0, sizeof(input->slots));
	input->active_count = 0;
	input->max_fingers = 0;
}

static int
touch_slot_index(struct libinput_event_touch *touch)
{
	int slot = libinput_event_touch_get_seat_slot(touch);

	return slot >= 0 && slot < MAX_TOUCH_SLOTS ? slot : -1;
}

static void
touch_coordinates(struct daemon_state *state,
    struct libinput_event_touch *touch, double *x, double *y,
    int *width, int *height)
{
	raw_output_size(state, width, height);
	*x = libinput_event_touch_get_x_transformed(touch, (uint32_t)*width);
	*y = libinput_event_touch_get_y_transformed(touch, (uint32_t)*height);
}

static void
emit_touch_event(struct daemon_state *state, char phase, int fingers,
    double start_x, double start_y, double end_x, double end_y)
{
	struct gesture_event gesture = {0};

	gesture.phase = phase;
	gesture.fingers = fingers;
	gesture.start_x = start_x;
	gesture.start_y = start_y;
	gesture.end_x = end_x;
	gesture.end_y = end_y;
	raw_output_size(state, &gesture.screen_width, &gesture.screen_height);
	if (phase == 'D')
		handle_down(state, &gesture);
	else if (phase == 'P')
		handle_press(state, &gesture);
	else
		handle_release(state, &gesture);
}

static void
finish_touch_sequence(struct daemon_state *state)
{
	struct touch_input *input = &state->input;
	double start_x = 0;
	double start_y = 0;
	double end_x = 0;
	double end_y = 0;
	int count = 0;

	for (int i = 0; i < MAX_TOUCH_SLOTS; i++) {
		if (!input->slots[i].used)
			continue;
		start_x += input->slots[i].start_x;
		start_y += input->slots[i].start_y;
		end_x += input->slots[i].x;
		end_y += input->slots[i].y;
		count++;
	}
	if (count > 0) {
		emit_touch_event(state, 'R', input->max_fingers,
		    start_x / count, start_y / count, end_x / count, end_y / count);
	}
	cancel_right_click(state);
	reset_touch_input(input);
}

static void
handle_libinput_touch(struct daemon_state *state, struct libinput_event *event)
{
	struct touch_input *input = &state->input;
	enum libinput_event_type type = libinput_event_get_type(event);
	struct libinput_event_touch *touch;
	struct touch_slot *slot;
	double x;
	double y;
	double distance;
	int width;
	int height;
	int index;

	if (type == LIBINPUT_EVENT_TOUCH_CANCEL) {
		cancel_right_click(state);
		finish_touch_sequence(state);
		return;
	}
	if (type != LIBINPUT_EVENT_TOUCH_DOWN &&
	    type != LIBINPUT_EVENT_TOUCH_MOTION &&
	    type != LIBINPUT_EVENT_TOUCH_UP)
		return;
	touch = libinput_event_get_touch_event(event);
	index = touch_slot_index(touch);
	if (index < 0)
		return;
	slot = &input->slots[index];
	if (type == LIBINPUT_EVENT_TOUCH_DOWN) {
		if (input->active_count == 0) {
			/*
			 * Output transforms commit asynchronously. Refresh before
			 * scaling the first coordinates so a post-rotation touch
			 * cannot use the daemon's previous orientation.
			 */
			refresh_output(state);
			reset_touch_input(input);
		}
		touch_coordinates(state, touch, &x, &y, &width, &height);
		slot->used = true;
		slot->active = true;
		slot->start_x = x;
		slot->start_y = y;
		slot->x = x;
		slot->y = y;
		input->active_count++;
		if (input->active_count > input->max_fingers)
			input->max_fingers = input->active_count;
		if (input->active_count > 1) {
			state->tiled_hold_deadline = 0;
			cancel_right_click(state);
		}
		if (input->active_count == 1)
			emit_touch_event(state, 'D', 1, slot->start_x, slot->start_y,
			    slot->x, slot->y);
		return;
	}
	if (!slot->used || !slot->active)
		return;
	if (type == LIBINPUT_EVENT_TOUCH_MOTION) {
		touch_coordinates(state, touch, &x, &y, &width, &height);
		slot->x = x;
		slot->y = y;
		if (input->max_fingers != 1)
			return;
		distance = hypot(slot->x - slot->start_x, slot->y - slot->start_y) /
		    (state->output.scale > 0 ? state->output.scale : 1.0);
		if (distance >= TOUCH_MOTION_SLOP) {
			cancel_right_click(state);
			emit_touch_event(state, 'P', 1, slot->start_x, slot->start_y,
			    slot->x, slot->y);
		}
		return;
	}
	slot->active = false;
	if (input->active_count > 0)
		input->active_count--;
	if (input->active_count == 0)
		finish_touch_sequence(state);
}

static int
init_touch_input(struct daemon_state *state)
{
	const char *path = getenv("CTLST_TOUCH_DEVICE");
	char discovered[256] = {0};

	if (path == NULL || path[0] == '\0' || strcmp(path, "auto") == 0) {
		glob_t devices = {0};
		if (glob("/dev/input/event*", 0, NULL, &devices) == 0) {
			for (size_t i = 0; i < devices.gl_pathc; i++) {
				unsigned char properties[INPUT_PROP_MAX / 8 + 1] = {0};
				struct input_absinfo axis;
				int fd = open(devices.gl_pathv[i], O_RDONLY | O_CLOEXEC);
				if (fd < 0)
					continue;
				bool touch = ioctl(fd, EVIOCGPROP(sizeof(properties)), properties) >= 0 &&
				    (properties[INPUT_PROP_DIRECT / 8] & (1U << (INPUT_PROP_DIRECT % 8))) &&
				    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &axis) >= 0 &&
				    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &axis) >= 0;
				close(fd);
				if (touch) {
					snprintf(discovered, sizeof(discovered), "%s", devices.gl_pathv[i]);
					break;
				}
			}
		}
		globfree(&devices);
		path = discovered;
	}
	state->input.fd = -1;
	if (access(path, R_OK) != 0) {
		if (state->debug)
			fprintf(stderr, "touch input absent: %s\n", path);
		return -1;
	}
	state->input.context = libinput_path_create_context(&input_interface, state);
	if (state->input.context == NULL)
		return -1;
	state->input.device = libinput_path_add_device(state->input.context, path);
	if (state->input.device == NULL) {
		libinput_unref(state->input.context);
		state->input.context = NULL;
		if (state->debug)
			fprintf(stderr, "touch input unavailable: %s\n", path);
		return -1;
	}
	(void)libinput_device_config_send_events_set_mode(state->input.device,
	    LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	state->input.fd = libinput_get_fd(state->input.context);
	reset_touch_input(&state->input);
	if (state->debug)
		fprintf(stderr, "touch input ready: %s fd=%d\n", path,
		    state->input.fd);
	return 1;
}

static void
dispatch_touch_input(struct daemon_state *state)
{
	struct libinput_event *event;

	if (state->input.context == NULL)
		return;
	if (libinput_dispatch(state->input.context) != 0)
		return;
	while ((event = libinput_get_event(state->input.context)) != NULL) {
		handle_libinput_touch(state, event);
		libinput_event_destroy(event);
	}
}

static void
destroy_touch_input(struct touch_input *input)
{
	if (input->device != NULL)
		libinput_path_remove_device(input->device);
	if (input->context != NULL)
		libinput_unref(input->context);
	input->device = NULL;
	input->context = NULL;
	input->fd = -1;
}

static bool
point_is_titlebar(const struct node *node, struct point point)
{
	int top;

	if (node->fullscreen != 0 || node->deco_height <= 0)
		return false;
	top = node->rect.y - node->deco_height;
	return point.x >= node->rect.x - 6 &&
	    point.x <= node->rect.x + node->rect.width + 6 &&
	    point.y >= top - 6 && point.y <= node->rect.y + 4;
}

static bool
point_is_titlebar_corner(const struct node *node, struct point point)
{
	return point_is_titlebar(node, point) &&
	    (point.x <= node->rect.x + 48 ||
	    point.x >= node->rect.x + node->rect.width - 48);
}

static struct rect
usable_workspace_rect(const struct daemon_state *state)
{
	struct rect rect = state->workspace_rect;
	int safe_top = state->output.rect.y + SHELL_TOP_BAR_HEIGHT;
	int safe_bottom = state->output.rect.y + state->output.rect.height -
	    SHELL_BOTTOM_BAR_HEIGHT;
	int bottom = rect.y + rect.height;

	if (rect.y < safe_top) {
		rect.height -= safe_top - rect.y;
		rect.y = safe_top;
	}
	if (bottom > safe_bottom)
		rect.height -= bottom - safe_bottom;
	if (rect.height < 1)
		return state->workspace_rect;
	return rect;
}

static bool
point_edge_top(const struct daemon_state *state, struct point point)
{
	struct rect rect = usable_workspace_rect(state);

	return point.y <= rect.y + 24;
}

static bool
point_edge_bottom(const struct daemon_state *state, struct point point)
{
	struct rect rect = usable_workspace_rect(state);

	return point.y >= rect.y + rect.height - 24;
}

static bool
send_drawer_command(struct daemon_state *state, char command)
{
	struct sockaddr_un address = {0};
	ssize_t sent;

	if (state->drawer_fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, state->drawer_path);
	sent = sendto(state->drawer_fd, &command, 1, 0,
	    (struct sockaddr *)&address, sizeof(address));
	if (sent == 1 && command == 'S')
		state->drawer_visible = true;
	else if (sent == 1 && command == 'H')
		state->drawer_visible = false;
	return sent == 1;
}

static bool
send_drawer_progress(struct daemon_state *state, int top_margin)
{
	struct sockaddr_un address = {0};
	char message[32];
	int length;
	ssize_t sent;

	length = snprintf(message, sizeof(message), "P %d", top_margin);
	if (length <= 0 || length >= (int)sizeof(message))
		return false;
	if (state->drawer_fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, state->drawer_path);
	sent = sendto(state->drawer_fd, message, (size_t)length, 0,
	    (struct sockaddr *)&address, sizeof(address));
	if (sent == length)
		state->drawer_visible = true;
	return sent == length;
}

static bool
send_dock_message(const struct daemon_state *state, const char *message)
{
	struct sockaddr_un address = {0};
	size_t length = strlen(message);
	ssize_t sent;

	if (state->drawer_fd < 0 || length == 0)
		return false;
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, state->dock_path);
	sent = sendto(state->drawer_fd, message, length, 0,
	    (struct sockaddr *)&address, sizeof(address));
	return sent == (ssize_t)length;
}

static bool
send_shade_message(struct daemon_state *state, const char *message)
{
	struct sockaddr_un address = {0};
	ssize_t sent;
	size_t length = strlen(message);
	int fd;

	if (length == 0)
		return false;
	fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, state->shade_path);
	sent = sendto(fd, message, length, 0,
	    (struct sockaddr *)&address, sizeof(address));
	if (sent != (ssize_t)length && message[0] != 'H' && message[0] != 'T' &&
	    message[0] != 'P' &&
	    run_command(state, "exec env GSK_RENDERER=gl "
	    "$HOME/.local/bin/ctlstshade")) {
		const struct timespec retry_delay = {
			.tv_nsec = 10 * 1000 * 1000,
		};

		for (int attempt = 0; attempt < 30 &&
		    sent != (ssize_t)length; attempt++) {
			nanosleep(&retry_delay, NULL);
			sent = sendto(fd, message, length, 0,
			    (struct sockaddr *)&address, sizeof(address));
		}
	}
	close(fd);
	if (sent == (ssize_t)length && (message[0] == 'S' || message[0] == 'W'))
		state->shade_visible = true;
	else if (sent == (ssize_t)length && message[0] == 'H')
		state->shade_visible = false;
	return sent == (ssize_t)length;
}

static bool
send_shade_command(struct daemon_state *state, char command)
{
	char message[2] = {command, '\0'};

	return send_shade_message(state, message);
}

static bool
send_overview_message(const struct daemon_state *state, const char *message)
{
	struct sockaddr_un address = {0};
	ssize_t sent;
	size_t length = strlen(message);
	int fd;

	fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, state->overview_path);
	sent = sendto(fd, message, length, 0,
	    (struct sockaddr *)&address, sizeof(address));
	close(fd);
	return sent == (ssize_t)length;
}

static bool
send_drop_overlay_message(const struct daemon_state *state,
    const char *message)
{
	struct sockaddr_un address = {0};
	ssize_t sent;
	size_t length = strlen(message);
	int fd;

	fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, state->drop_overlay_path);
	sent = sendto(fd, message, length, 0,
	    (struct sockaddr *)&address, sizeof(address));
	close(fd);
	return sent == (ssize_t)length;
}

static bool
send_action_overlay_message(const struct daemon_state *state,
    const char *message)
{
	struct sockaddr_un address = {0};
	ssize_t sent;
	size_t length = strlen(message);
	int fd;

	fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, state->action_overlay_path);
	sent = sendto(fd, message, length, 0,
	    (struct sockaddr *)&address, sizeof(address));
	close(fd);
	return sent == (ssize_t)length;
}

static int
drop_side_width(const struct daemon_state *state)
{
	struct rect rect = usable_workspace_rect(state);
	int width = rect.width * 18 / 100;

	if (width < DROP_SIDE_MIN_WIDTH)
		return DROP_SIDE_MIN_WIDTH;
	if (width > DROP_SIDE_MAX_WIDTH)
		return DROP_SIDE_MAX_WIDTH;
	return width;
}

static int
drop_side_height(const struct daemon_state *state)
{
	struct rect rect = usable_workspace_rect(state);
	int height = rect.height * 44 / 100;

	if (height < DROP_SIDE_MIN_HEIGHT)
		height = DROP_SIDE_MIN_HEIGHT;
	if (height > DROP_SIDE_MAX_HEIGHT)
		height = DROP_SIDE_MAX_HEIGHT;
	if (height > rect.height - 32)
		height = rect.height - 32;
	return height;
}

static void
drop_workspace_range(const struct workspace_list *list, int width, int *start, int *count)
{
	int available = list == NULL || list->count <= 1 ? 0 :
	    (int)list->count - 1;
	int focused = list == NULL ? -1 : workspace_list_index(list, list->focused);

	int capacity = (width - DROP_CHARM_SIDE_MARGIN * 2 - DROP_CHARM_NEW_WIDTH) /
	    (DROP_CHARM_MIN_WIDTH + DROP_CHARM_GAP);
	if (capacity < 0)
		capacity = 0;
	if (capacity > DROP_CHARM_MAX_VISIBLE)
		capacity = DROP_CHARM_MAX_VISIBLE;
	*count = available < capacity ? available : capacity;
	*start = 1;
	if (available <= *count || focused < 1)
		return;
	*start = focused - *count / 2;
	if (*start < 1)
		*start = 1;
	if (*start + *count > (int)list->count)
		*start = (int)list->count - *count;
}

static int
drop_workspace_index_at(struct daemon_state *state, struct point point)
{
	const struct workspace_list *list = cached_workspace_list(state);
	struct rect rect = usable_workspace_rect(state);
	int start;
	int count;
	int available_width;
	int charm_width;
	int row_width;
	int left;
	int top = rect.y + DROP_CHARM_TOP_MARGIN;

	drop_workspace_range(list, usable_workspace_rect(state).width, &start, &count);
	if (count == 0 || point.y < top ||
	    point.y > top + DROP_CHARM_HEIGHT)
		return -1;
	available_width = rect.width - DROP_CHARM_SIDE_MARGIN * 2 -
	    DROP_CHARM_NEW_WIDTH - DROP_CHARM_GAP * count;
	charm_width = available_width / count;
	if (charm_width < DROP_CHARM_MIN_WIDTH)
		charm_width = DROP_CHARM_MIN_WIDTH;
	if (charm_width > DROP_CHARM_MAX_WIDTH)
		charm_width = DROP_CHARM_MAX_WIDTH;
	row_width = count * charm_width + count * DROP_CHARM_GAP +
	    DROP_CHARM_NEW_WIDTH;
	left = rect.x + (rect.width - row_width) / 2;
	for (int slot = 0; slot < count; slot++) {
		int charm_left = left + slot * (charm_width + DROP_CHARM_GAP);

		if (point.x >= charm_left &&
		    point.x <= charm_left + charm_width)
			return start + slot;
	}
	return -1;
}

static bool
drop_new_task_at(struct daemon_state *state, struct point point)
{
	const struct workspace_list *list = cached_workspace_list(state);
	struct rect rect = usable_workspace_rect(state);
	int start;
	int count;
	int available_width;
	int charm_width;
	int row_width;
	int left;
	int new_left;
	int top = rect.y + DROP_CHARM_TOP_MARGIN;

	drop_workspace_range(list, usable_workspace_rect(state).width, &start, &count);
	(void)start;
	available_width = rect.width - DROP_CHARM_SIDE_MARGIN * 2 -
	    DROP_CHARM_NEW_WIDTH - DROP_CHARM_GAP * count;
	charm_width = count > 0 ? available_width / count :
	    DROP_CHARM_MAX_WIDTH;
	if (charm_width < DROP_CHARM_MIN_WIDTH)
		charm_width = DROP_CHARM_MIN_WIDTH;
	if (charm_width > DROP_CHARM_MAX_WIDTH)
		charm_width = DROP_CHARM_MAX_WIDTH;
	row_width = count * charm_width + count * DROP_CHARM_GAP +
	    DROP_CHARM_NEW_WIDTH;
	left = rect.x + (rect.width - row_width) / 2;
	new_left = left + count * (charm_width + DROP_CHARM_GAP);
	return point.x >= new_left &&
	    point.x <= new_left + DROP_CHARM_NEW_WIDTH &&
	    point.y >= top && point.y <= top + DROP_CHARM_HEIGHT;
}

static enum drop_target
drop_target_at(struct daemon_state *state, struct point point)
{
	struct rect rect = usable_workspace_rect(state);
	int side_width = drop_side_width(state);
	int side_height = drop_side_height(state);
	int side_top = rect.y + (rect.height - side_height) / 2;
	int close_left = rect.x + (rect.width - DROP_CLOSE_WIDTH) / 2;
	int close_top = rect.y + rect.height -
	    DROP_CLOSE_BOTTOM_MARGIN - DROP_CLOSE_HEIGHT;

	if (point.x >= close_left &&
	    point.x <= close_left + DROP_CLOSE_WIDTH &&
	    point.y >= close_top &&
	    point.y <= close_top + DROP_CLOSE_HEIGHT)
		return DROP_CLOSE;
	if (drop_workspace_index_at(state, point) >= 0)
		return DROP_WORKSPACE_DIRECT;
	if (drop_new_task_at(state, point))
		return DROP_NEW_TASK;
	if (point.y < side_top || point.y > side_top + side_height)
		return DROP_NONE;
	if (point.x <= rect.x + side_width)
		return DROP_WORKSPACE_LEFT;
	if (point.x >= rect.x + rect.width - side_width)
		return DROP_WORKSPACE_RIGHT;
	return DROP_NONE;
}

static void
drop_target_code(enum drop_target target, int workspace_index,
    char *code, size_t code_size)
{
	switch (target) {
	case DROP_WORKSPACE_LEFT:
		snprintf(code, code_size, "L");
		return;
	case DROP_WORKSPACE_RIGHT:
		snprintf(code, code_size, "R");
		return;
	case DROP_WORKSPACE_DIRECT:
		snprintf(code, code_size, "W%d", workspace_index);
		return;
	case DROP_NEW_TASK:
		snprintf(code, code_size, "T");
		return;
	case DROP_CLOSE:
		snprintf(code, code_size, "X");
		return;
	default:
		snprintf(code, code_size, "N");
		return;
	}
}

static void
show_drop_overlay(struct daemon_state *state)
{
	const struct workspace_list *list;
	char message[1024];
	struct rect rect = usable_workspace_rect(state);
	int start;
	int count;
	int focused = -1;
	int length;

	(void)refresh_workspace_cache(state);
	list = cached_workspace_list(state);
	drop_workspace_range(list, usable_workspace_rect(state).width, &start, &count);
	length = snprintf(message, sizeof(message), "S %d %d %d %d ",
	    rect.x, rect.y, rect.width, rect.height);
	if (list != NULL)
		focused = workspace_list_index(list, list->focused);
	length += snprintf(message + length, sizeof(message) - (size_t)length,
	    "%d", focused >= start && focused < start + count ?
	    focused - start : -1);
	for (int slot = 0; slot < count && length > 0 &&
	    (size_t)length < sizeof(message); slot++)
		length += snprintf(message + length,
		    sizeof(message) - (size_t)length, " %s",
		    list->names[start + slot]);
	send_drop_overlay_message(state, message);
}

static void
update_drop_overlay(struct daemon_state *state, enum drop_target target,
    struct point point)
{
	const struct workspace_list *list;
	char code[24];
	char message[32];
	int workspace_index = target == DROP_WORKSPACE_DIRECT ?
	    drop_workspace_index_at(state, point) : -1;
	int visible_start = 0;
	int visible_count = 0;

	if (state->gesture.drop_target == target &&
	    state->gesture.drop_workspace_index == workspace_index)
		return;
	state->gesture.drop_target = target;
	state->gesture.drop_workspace_index = workspace_index;
	list = cached_workspace_list(state);
	drop_workspace_range(list, usable_workspace_rect(state).width, &visible_start, &visible_count);
	(void)visible_count;
	drop_target_code(target,
	    workspace_index >= 0 ? workspace_index - visible_start : -1,
	    code, sizeof(code));
	snprintf(message, sizeof(message), "A %s", code);
	send_drop_overlay_message(state, message);
}

static void
hide_drop_overlay(const struct daemon_state *state)
{
	send_drop_overlay_message(state, "H");
}

static void
toggle_keyboard(struct daemon_state *state)
{
	run_command(state, "exec $CTLST_LIBEXEC_DIR/"
	    "keyboard-control toggle");
}

static void
hide_keyboard(struct daemon_state *state)
{
	run_command(state, "exec $CTLST_LIBEXEC_DIR/"
	    "keyboard-control hide");
}

static bool
clamp_floating(struct daemon_state *state, int64_t node_id)
{
	struct node node;
	struct rect usable = usable_workspace_rect(state);
	char command[256];
	int width;
	int height;
	int max_width;
	int max_height;
	int min_x;
	int max_x;
	int min_top;
	int max_top;
	int new_x;
	int new_top;
	int move_x;
	int move_top;

	if (!load_tree_node(state, node_id, &node) ||
	    !node.floating || node.fullscreen != 0)
		return false;

	max_width = usable.width - 4;
	max_height = usable.height - node.deco_height - 4;
	if (max_width <= 0 || max_height <= 0)
		return false;
	width = node.rect.width < max_width ? node.rect.width : max_width;
	height = node.rect.height < max_height ? node.rect.height : max_height;

	min_x = usable.x + 2;
	max_x = usable.x + usable.width - width - 2;
	min_top = usable.y + 2;
	max_top = usable.y + usable.height -
	    height - node.deco_height - 2;
	new_x = node.rect.x;
	new_top = node.rect.y - node.deco_height;
	if (new_x < min_x)
		new_x = min_x;
	if (new_x > max_x)
		new_x = max_x;
	if (new_top < min_top)
		new_top = min_top;
	if (new_top > max_top)
		new_top = max_top;
	move_x = new_x - state->workspace_rect.x;
	move_top = new_top - state->workspace_rect.y;

	snprintf(command, sizeof(command),
	    "[con_id=%" PRId64 "] resize set width %d px height %d px, "
	    "move position %d %d",
	    node.id, width, height, move_x, move_top);
	return run_command(state, command);
}

static bool
set_floating(struct daemon_state *state, int64_t node_id, bool enable,
    bool center)
{
	char command[256];

	if (enable) {
		snprintf(command, sizeof(command),
		    "[con_id=%" PRId64 "] floating enable, "
		    "title_format \"%s\"%s",
		    node_id, FLOATING_TITLE_FORMAT,
		    center ? ", move position center" : "");
	} else {
		snprintf(command, sizeof(command),
		    "[con_id=%" PRId64 "] floating disable, "
		    "title_format \"%s\"",
		    node_id, TILED_TITLE_FORMAT);
	}
	return run_command(state, command);
}

static bool
set_compact_floating(struct daemon_state *state, int64_t node_id)
{
	struct rect usable = usable_workspace_rect(state);
	char command[320];
	int width = usable.width * 70 / 100;
	int height = usable.height * 58 / 100;
	int max_width = usable.width - 24;
	int max_height = usable.height - 24;
	int minimum_height = usable.height < 500 ? 180 : 300;

	if (max_width <= 0 || max_height <= 0)
		return false;
	if (width < 240)
		width = 240;
	if (height < minimum_height)
		height = minimum_height;
	if (width > max_width)
		width = max_width;
	if (height > max_height)
		height = max_height;
	snprintf(command, sizeof(command),
	    "[con_id=%" PRId64 "] floating enable, "
	    "resize set width %d px height %d px, move position center, "
	    "title_format \"%s\"",
	    node_id, width, height, FLOATING_TITLE_FORMAT);
	return run_command(state, command);
}

static bool
toggle_floating(struct daemon_state *state)
{
	struct node node;

	if (!load_tree_node(state, 0, &node))
		return false;
	if (node.floating)
		return set_floating(state, node.id, false, false);
	return set_compact_floating(state, node.id);
}

static bool
toggle_fullscreen(struct daemon_state *state)
{
	return run_command(state, "fullscreen toggle");
}

static bool
set_controls_revealed(struct daemon_state *state, bool visible)
{
	bool success;

	if (visible) {
		success = run_command(state, "bar mode hide controls") &&
		    run_command(state, "bar hidden_state show controls");
		if (success) {
			state->controls_revealed = true;
			state->controls_reveal_deadline =
			    monotonic_us() + BAR_REVEAL_US;
		}
		return success;
	}
	success = run_command(state, "bar mode dock controls");
	state->controls_revealed = false;
	state->controls_reveal_deadline = 0;
	return success;
}

static void
reveal_workspace_wheel_dock(struct daemon_state *state)
{
	if (state->gesture.mode != GESTURE_WORKSPACE_WHEEL_PENDING &&
	    state->gesture.mode != GESTURE_WORKSPACE_WHEEL)
		return;
	if (state->gesture.wheel_dock_revealed)
		return;
	state->gesture.wheel_dock_revealed = true;
	state->wheel_reveal_deadline = 0;
	block_shell_clicks(state);
	/* Full task-pill morph (dash → rotary dock). */
	send_dock_message(state, "E");
	pulse_workspace_haptic(state);
}

static void
update_workspace_wheel_hold_inflate(struct daemon_state *state)
{
	char message[32];
	int64_t held_us;
	int progress;

	if (state->gesture.mode != GESTURE_WORKSPACE_WHEEL_PENDING ||
	    state->gesture.wheel_dock_revealed ||
	    state->touch_down_us == 0)
		return;
	held_us = monotonic_us() - state->touch_down_us;
	if (held_us < 0)
		held_us = 0;
	if (held_us >= WORKSPACE_WHEEL_REVEAL_HOLD_US) {
		reveal_workspace_wheel_dock(state);
		return;
	}
	progress = (int)((held_us * WORKSPACE_WHEEL_INFLATE_MAX_PROGRESS) /
	    WORKSPACE_WHEEL_REVEAL_HOLD_US);
	if (progress < 0)
		progress = 0;
	if (progress > WORKSPACE_WHEEL_INFLATE_MAX_PROGRESS)
		progress = WORKSPACE_WHEEL_INFLATE_MAX_PROGRESS;
	snprintf(message, sizeof(message), "P %d", progress);
	send_dock_message(state, message);
}

static bool
arm_expired_wheel_reveal(struct daemon_state *state)
{
	if (state->gesture.mode == GESTURE_WORKSPACE_WHEEL_PENDING &&
	    !state->gesture.wheel_dock_revealed)
		update_workspace_wheel_hold_inflate(state);
	if (state->wheel_reveal_deadline == 0 ||
	    state->wheel_reveal_deadline > monotonic_us())
		return false;
	state->wheel_reveal_deadline = 0;
	if (state->input.active_count != 1 ||
	    state->gesture.mode != GESTURE_WORKSPACE_WHEEL_PENDING)
		return false;
	reveal_workspace_wheel_dock(state);
	return true;
}

static void
reset_gesture(struct daemon_state *state)
{
	if (state->gesture.mode == GESTURE_WORKSPACE_WHEEL ||
	    state->gesture.mode == GESTURE_WORKSPACE_WHEEL_PENDING)
		send_dock_message(state, "H");
	if (state->gesture.mode == GESTURE_TILED_DRAG)
		run_command(state, "tiling_drag disable");
	state->tiled_hold_deadline = 0;
	cancel_right_click(state);
	state->right_click_fired = false;
	state->wheel_reveal_deadline = 0;
	if (state->gesture.mode == GESTURE_DRAG ||
	    state->gesture.mode == GESTURE_TILED_DRAG ||
	    state->gesture.mode == GESTURE_SNAPPED)
		hide_drop_overlay(state);
	if (state->gesture.mode == GESTURE_ACTION_ORBIT)
		send_action_overlay_message(state, "H");
	/*
	 * Incomplete overview previews are cancelled by the finish path.
	 * If an external surface steals the session, hide any half-open preview.
	 */
	if (state->gesture.mode == GESTURE_OVERVIEW_PULL &&
	    !state->overview_visible)
		send_overview_message(state, "H");
	memset(&state->gesture, 0, sizeof(state->gesture));
	state->gesture.mode = GESTURE_NONE;
}

static bool
arm_tiled_drag(struct daemon_state *state, const struct node *node)
{
	if (node->floating || state->gesture.mode != GESTURE_WATCH)
		return false;
	state->tiled_hold_deadline = 0;
	if (!run_command(state, "tiling_drag enable"))
		return false;
	state->gesture.mode = GESTURE_TILED_DRAG;
	state->gesture.node_id = node->id;
	state->gesture.drop_target = DROP_NONE;
	show_drop_overlay(state);
	block_shell_clicks(state);
	pulse_workspace_haptic(state);
	return true;
}

static bool
arm_expired_tiled_hold(struct daemon_state *state)
{
	struct node node;

	if (state->tiled_hold_deadline == 0 ||
	    state->tiled_hold_deadline > monotonic_us())
		return false;
	state->tiled_hold_deadline = 0;
	if (state->input.active_count != 1 ||
	    state->gesture.mode != GESTURE_WATCH ||
	    !load_tree_node(state, state->gesture.node_id, &node))
		return false;
	return arm_tiled_drag(state, &node);
}

static bool
same_sequence(const struct gesture_state *gesture, struct point start)
{
	return gesture->mode != GESTURE_NONE &&
	    fabs(gesture->start.x - start.x) < 2.0 &&
	    fabs(gesture->start.y - start.y) < 2.0;
}

static bool
try_fullscreen_motion(struct daemon_state *state, const struct node *node,
    struct point start, struct point end)
{
	double dx = end.x - start.x;
	double dy = end.y - start.y;
	char command[128];

	if (node->fullscreen == 0 && point_is_titlebar(node, start) &&
	    !point_is_titlebar_corner(node, start) &&
	    -dy >= 24 && fabs(dx) <= -dy * 1.25) {
		snprintf(command, sizeof(command),
		    "[con_id=%" PRId64 "] fullscreen enable", node->id);
		return run_command(state, command);
	}
	return false;
}

static bool
begin_resize(struct daemon_state *state, const struct node *node,
    struct point start)
{
	struct gesture_state *gesture = &state->gesture;
	int right;
	int bottom;

	if (!node->floating || node->fullscreen != 0 ||
	    strncmp(node->app_id, "waydroid.", 9) != 0)
		return false;
	right = node->rect.x + node->rect.width;
	bottom = node->rect.y + node->rect.height;
	if (start.x < right - 28 || start.x > right + 8 ||
	    start.y < bottom - 28 || start.y > bottom + 8)
		return false;

	gesture->mode = GESTURE_RESIZE;
	gesture->node_id = node->id;
	gesture->resize_start_x = (int)start.x;
	gesture->resize_start_y = (int)start.y;
	gesture->resize_width = node->window_width;
	gesture->resize_height = node->window_height;
	gesture->resize_max_width = state->workspace_rect.width - 20;
	gesture->resize_max_height = state->workspace_rect.height -
	    node->deco_height - 20;
	gesture->resize_move_x = node->rect.x - state->workspace_rect.x;
	gesture->resize_move_top = node->rect.y - node->deco_height -
	    state->workspace_rect.y;
	return true;
}

static void
update_resize(struct daemon_state *state, struct point end, bool final)
{
	struct gesture_state *gesture = &state->gesture;
	char command[256];
	int64_t now = monotonic_us();
	int width;
	int height;

	if (!final && gesture->last_resize_us != 0 &&
	    now - gesture->last_resize_us < RESIZE_FRAME_US)
		return;
	width = gesture->resize_width + (int)end.x - gesture->resize_start_x;
	height = gesture->resize_height + (int)end.y - gesture->resize_start_y;
	if (width < 240)
		width = 240;
	if (height < 320)
		height = 320;
	if (width > gesture->resize_max_width)
		width = gesture->resize_max_width;
	if (height > gesture->resize_max_height)
		height = gesture->resize_max_height;
	snprintf(command, sizeof(command),
	    "[con_id=%" PRId64 "] resize set width %d px height %d px, "
	    "move position %d %d",
	    gesture->node_id, width, height,
	    gesture->resize_move_x, gesture->resize_move_top);
	run_command(state, command);
	gesture->last_resize_us = now;
	if (final)
		clamp_floating(state, gesture->node_id);
}

static bool
begin_drag(struct daemon_state *state, struct node *node, struct point start,
    struct point end)
{
	struct gesture_state *gesture = &state->gesture;
	double dx = end.x - start.x;
	double dy = end.y - start.y;
	int64_t held_us = state->touch_down_us == 0 ? 0 :
	    monotonic_us() - state->touch_down_us;

	if (!point_is_titlebar(node, start))
		return false;
	if (!node->floating) {
		if (state->touch_down_us == 0 ||
		    held_us < WINDOW_DRAG_HOLD_US)
			return false;
		return arm_tiled_drag(state, node);
	}
	if (fabs(dx) >= fabs(dy) &&
	    (state->touch_down_us == 0 || held_us < WINDOW_DRAG_HOLD_US))
		return false;
	if (!refresh_workspace(state))
		return false;

	gesture->mode = GESTURE_DRAG;
	gesture->node_id = node->id;
	gesture->started_floating = true;
	gesture->allow_top = !point_edge_top(state, start);
	gesture->allow_bottom = !point_edge_bottom(state, start);
	gesture->drop_target = DROP_NONE;
	show_drop_overlay(state);
	return true;
}

static void
update_drag(struct daemon_state *state, struct point end)
{
	struct gesture_state *gesture = &state->gesture;
	struct node node;
	struct rect usable = usable_workspace_rect(state);
	char command[192];
	bool top = point_edge_top(state, end);
	bool bottom = point_edge_bottom(state, end);
	enum drop_target target = drop_target_at(state, end);

	update_drop_overlay(state, target, end);
	if (target == DROP_NONE &&
	    ((top && gesture->allow_top) ||
	    (bottom && gesture->allow_bottom))) {
		if (set_floating(state, gesture->node_id, false, false))
			gesture->mode = GESTURE_SNAPPED;
		hide_drop_overlay(state);
		return;
	}
	if (!top)
		gesture->allow_top = true;
	if (!bottom)
		gesture->allow_bottom = true;

	if (end.y <= usable.y + 28 &&
	    load_tree_node(state, gesture->node_id, &node) &&
	    node.rect.y - node.deco_height < usable.y + 2) {
		int move_x = node.rect.x - state->workspace_rect.x;
		int move_top = usable.y - state->workspace_rect.y + 2;
		snprintf(command, sizeof(command),
		    "[con_id=%" PRId64 "] move position %d %d",
		    gesture->node_id, move_x, move_top);
		run_command(state, command);
	}
}

static bool
move_window_to_existing_task(struct daemon_state *state, int64_t node_id,
    int workspace_index, bool started_floating, bool focus)
{
	const struct workspace_list *list = cached_workspace_list(state);
	char command[384];
	const char *workspace;

	if (list == NULL || workspace_index <= 0 ||
	    workspace_index >= (int)list->count)
		return false;
	workspace = list->names[workspace_index];
	if (strcmp(workspace, state->workspace_name) == 0)
		return true;
	if (started_floating) {
		if (focus)
			snprintf(command, sizeof(command),
			    "[con_id=%" PRId64 "] move container to workspace "
			    "%.*s; workspace %.*s",
			    node_id, 48, workspace, 48, workspace);
		else
			snprintf(command, sizeof(command),
			    "[con_id=%" PRId64 "] move container to workspace %.*s",
			    node_id, 48, workspace);
	} else {
		if (focus)
			snprintf(command, sizeof(command),
			    "[con_id=%" PRId64 "] floating disable, title_format "
			    "\"%s\", move container to workspace %.*s; "
			    "workspace %.*s",
			    node_id, TILED_TITLE_FORMAT, 48, workspace,
			    48, workspace);
		else
			snprintf(command, sizeof(command),
			    "[con_id=%" PRId64 "] floating disable, title_format "
			    "\"%s\", move container to workspace %.*s",
			    node_id, TILED_TITLE_FORMAT, 48, workspace);
	}
	if (!run_command(state, command))
		return false;
	if (focus) {
		snprintf(state->workspace_name, sizeof(state->workspace_name),
		    "%s", workspace);
		state->workspace = numeric_workspace_name(workspace);
	}
	state->workspace_cache_valid = false;
	(void)refresh_workspace_cache(state);
	return true;
}

static void
finish_drag(struct daemon_state *state, struct point end)
{
	struct gesture_state *gesture = &state->gesture;
	enum drop_target target;
	char command[256];

	if (gesture->mode == GESTURE_SNAPPED)
		return;
	target = drop_target_at(state, end);
	if (target == DROP_CLOSE) {
		snprintf(command, sizeof(command),
		    "[con_id=%" PRId64 "] kill", gesture->node_id);
		run_command(state, command);
		return;
	}
	if (target == DROP_NEW_TASK) {
		(void)move_window_to_new_task(state, gesture->node_id, true, NULL, 0);
		return;
	}
	if (target == DROP_WORKSPACE_DIRECT) {
		(void)move_window_to_existing_task(state, gesture->node_id,
		    drop_workspace_index_at(state, end),
		    gesture->started_floating, true);
		return;
	}
	if (target == DROP_WORKSPACE_LEFT ||
	    target == DROP_WORKSPACE_RIGHT) {
		char workspace[MAX_WORKSPACE_NAME];
		int direction = target == DROP_WORKSPACE_LEFT ? -1 : 1;
		int windows;

		if (!refresh_workspace(state))
			return;
		windows = focused_workspace_window_count(state);
		if (windows > 1) {
			if (!gesture->started_floating) {
				snprintf(command, sizeof(command),
				    "[con_id=%" PRId64 "] floating disable, "
				    "title_format \"%s\"",
				    gesture->node_id, TILED_TITLE_FORMAT);
				if (!run_command(state, command))
					return;
			}
			(void)workspace_manager_move(state, gesture->node_id,
			    "new", false, workspace, sizeof(workspace));
			return;
		}
		if (!dynamic_app_target(state, direction, workspace,
		    sizeof(workspace)))
			return;
		if (gesture->started_floating) {
			snprintf(command, sizeof(command),
			    "[con_id=%" PRId64 "] move container to "
			    "workspace %.*s",
			    gesture->node_id, 48, workspace);
		} else {
			snprintf(command, sizeof(command),
			    "[con_id=%" PRId64 "] floating disable, "
			    "title_format \"%s\", move container to "
			    "workspace %.*s",
			    gesture->node_id, TILED_TITLE_FORMAT, 48, workspace);
		}
		run_command(state, command);
		return;
	}
	if (point_edge_top(state, end) || point_edge_bottom(state, end)) {
		set_floating(state, gesture->node_id, false, false);
		return;
	}
	snprintf(command, sizeof(command),
	    "[con_id=%" PRId64 "] focus", gesture->node_id);
	run_command(state, command);
	clamp_floating(state, gesture->node_id);
}

static double
bottom_pull_distance(struct point start, struct point end)
{
	return start.y - end.y;
}

static bool
runtime_file_exists(const struct daemon_state *state, const char *name)
{
	char path[256];

	if (snprintf(path, sizeof(path), "%s/%s", state->runtime_dir, name) >=
	    (int)sizeof(path))
		return false;
	return access(path, F_OK) == 0;
}

static bool
send_runtime_byte(const struct daemon_state *state, const char *name,
    char command)
{
	struct sockaddr_un address = {0};
	int fd;
	ssize_t sent;

	if (snprintf(address.sun_path, sizeof(address.sun_path), "%s/%s",
	    state->runtime_dir, name) >= (int)sizeof(address.sun_path))
		return false;
	fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	sent = sendto(fd, &command, 1, 0,
	    (struct sockaddr *)&address, sizeof(address));
	close(fd);
	return sent == 1;
}

static bool
send_runtime_message(const struct daemon_state *state, const char *name,
    const char *message)
{
	struct sockaddr_un address = {0};
	int fd;
	ssize_t sent;
	size_t length = strlen(message);

	if (length == 0 || length >= CONTROL_MESSAGE_SIZE)
		return false;
	if (snprintf(address.sun_path, sizeof(address.sun_path), "%s/%s",
	    state->runtime_dir, name) >= (int)sizeof(address.sun_path))
		return false;
	fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	sent = sendto(fd, message, length, 0,
	    (struct sockaddr *)&address, sizeof(address));
	close(fd);
	return sent == (ssize_t)length;
}

static void
notify_editable_tap(const struct daemon_state *state, struct point point)
{
	char message[96];
	int length;

	/*
	 * Most clients activate Wayland text-input-v3 directly. A few toolkit
	 * chrome controls keep an editable AT-SPI object focused after the user
	 * manually hides the keyboard, then emit no second focus event when that
	 * same control is tapped again. Forward only the tap coordinate; the
	 * focus daemon performs the generic editable hit-test.
	 */
	if (!runtime_file_exists(state, "ctlstkeyboard.suppressed"))
		return;
	length = snprintf(message, sizeof(message), "T %.3f %.3f %.3f",
	    point.x, point.y, state->output.scale > 0 ? state->output.scale : 1.0);
	if (length <= 0 || length >= (int)sizeof(message))
		return;
	(void)send_runtime_message(state, "ctlst-keyboard-focus.sock", message);
}

static bool
close_pidfile_process(const struct daemon_state *state, const char *name)
{
	char path[256];
	char comm_path[64];
	char buffer[32];
	char comm[32];
	char *end;
	ssize_t length;
	ssize_t comm_length;
	long pid;
	int fd;

	if (snprintf(path, sizeof(path), "%s/%s", state->runtime_dir, name) >=
	    (int)sizeof(path))
		return false;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	length = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (length <= 0)
		return false;
	buffer[length] = '\0';
	errno = 0;
	pid = strtol(buffer, &end, 10);
	if (errno != 0 || end == buffer || pid <= 1) {
		unlink(path);
		return false;
	}
	if (snprintf(comm_path, sizeof(comm_path), "/proc/%ld/comm", pid) >=
	    (int)sizeof(comm_path)) {
		unlink(path);
		return false;
	}
	fd = open(comm_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		unlink(path);
		return false;
	}
	comm_length = read(fd, comm, sizeof(comm) - 1);
	close(fd);
	if (comm_length <= 0) {
		unlink(path);
		return false;
	}
	comm[comm_length] = '\0';
	comm[strcspn(comm, "\r\n")] = '\0';
	if (strcmp(comm, "fuzzel") != 0) {
		unlink(path);
		return false;
	}
	if (kill((pid_t)pid, SIGTERM) < 0 && errno != ESRCH)
		return false;
	unlink(path);
	return true;
}

static void
go_home(struct daemon_state *state)
{
	send_runtime_byte(state, "ctlst-home.sock", 'E');
	/* Home ends transient task selection even when no wheel gesture owns it. */
	send_dock_message(state, "H");
	if (runtime_file_exists(state, "ctlstkeyboard.visible"))
		toggle_keyboard(state);
	close_pidfile_process(state, "sway-touch-session-menu.pid");
	if (runtime_file_exists(state, "ctlstpad.visible"))
		send_runtime_byte(state, "ctlstpad.sock", 'H');
	if (state->drawer_visible ||
	    runtime_file_exists(state, "ctlstdrawer.visible"))
		send_drawer_command(state, 'H');
	if (state->shade_visible ||
	    runtime_file_exists(state, "ctlstshade.visible"))
		send_shade_command(state, 'H');
	if (state->overview_visible ||
	    runtime_file_exists(state, "ctlstoverview.visible"))
		send_overview_message(state, "H");
	state->drawer_visible = false;
	state->shade_visible = false;
	state->overview_visible = false;
	state->dialer_visible = false;
	state->drawer_close_sequence = false;
	run_command(state, "fullscreen disable; workspace number 1");
	state->workspace = FIRST_WORKSPACE;
	snprintf(state->workspace_name, sizeof(state->workspace_name), "1");
}

static bool
run_home_navigation(struct daemon_state *state)
{
	if (runtime_file_exists(state, "ctlstlock.visible"))
		return false;
	go_home(state);
	return true;
}

static bool
run_back_navigation(struct daemon_state *state)
{
	struct node node;
	char command[128];
	bool have_node;
	if (runtime_file_exists(state, "ctlstlock.visible"))
		return false;
	if (runtime_file_exists(state, "ctlstkeyboard.visible")) {
		toggle_keyboard(state);
		pulse_workspace_haptic(state);
		return true;
	}
	if (state->overview_visible ||
	    runtime_file_exists(state, "ctlstoverview.visible")) {
		send_overview_message(state, "H");
		state->overview_visible = false;
		pulse_workspace_haptic(state);
		return true;
	}
	if (state->shade_visible ||
	    runtime_file_exists(state, "ctlstshade.visible")) {
		send_shade_command(state, 'H');
		state->shade_visible = false;
		pulse_workspace_haptic(state);
		return true;
	}
	if (state->drawer_visible ||
	    runtime_file_exists(state, "ctlstdrawer.visible")) {
		send_drawer_command(state, 'H');
		state->drawer_visible = false;
		state->drawer_close_sequence = false;
		pulse_workspace_haptic(state);
		return true;
	}
	if (runtime_file_exists(state, "ctlstpad.visible")) {
		send_runtime_byte(state, "ctlstpad.sock", 'H');
		pulse_workspace_haptic(state);
		return true;
	}
	if (state->dialer_visible ||
	    runtime_file_exists(state, "ctlstdialer.visible")) {
		run_command(state,
		    "exec gapplication action dev.ctlst.Dialer hide");
		state->dialer_visible = false;
		pulse_workspace_haptic(state);
		return true;
	}
	have_node = load_tree_node(state, 0, &node);
	if (have_node && strcmp(node.app_id, "dev.ctlst.Messages") == 0) {
		if (!run_command(state,
		    "exec gapplication action dev.ctlst.Messages back"))
			return false;
		pulse_workspace_haptic(state);
		return true;
	}
	if (have_node && node.fullscreen != 0) {
		snprintf(command, sizeof(command),
		    "[con_id=%" PRId64 "] fullscreen disable", node.id);
		if (!run_command(state, command))
			return false;
		pulse_workspace_haptic(state);
		return true;
	}


	/*
	 * Alt+Left is the focused-window equivalent of mouse Back. Browsers and
	 * standard Linux navigation views already understand it; CTLST apps bind
	 * the same chord to their own page stacks.
	 */
	if (!run_command(state, "exec wtype -M alt -k Left -m alt"))
		return false;
	pulse_workspace_haptic(state);
	return true;
}

static bool
close_focused_app(struct daemon_state *state)
{
	struct node node;
	char command[192];
	int windows;
	const char *workspace;

	if (runtime_file_exists(state, "ctlstlock.visible"))
		return false;
	if (!load_tree_node(state, 0, &node)) {
		if (state->debug)
			fprintf(stderr, "close: focused node unavailable\n");
		return false;
	}
	workspace = safe_workspace_name(node.workspace_name) ?
	    node.workspace_name : state->workspace_name;
	windows = node.workspace_windows;
	if (state->debug)
		fprintf(stderr, "close: node=%" PRId64 " workspace=%s windows=%d\n",
		    node.id, workspace, windows);
	if (windows <= 1 && strcmp(workspace, "1") != 0) {
		snprintf(command, sizeof(command),
		    "[con_id=%" PRId64 "] kill; workspace number 1", node.id);
		if (!run_command(state, command))
			return false;
		state->workspace = FIRST_WORKSPACE;
		snprintf(state->workspace_name, sizeof(state->workspace_name), "1");
	} else {
		snprintf(command, sizeof(command),
		    "[con_id=%" PRId64 "] kill", node.id);
		if (!run_command(state, command))
			return false;
	}
	if (runtime_file_exists(state, "ctlstkeyboard.visible"))
		hide_keyboard(state);
	if (runtime_file_exists(state, "ctlstpad.visible"))
		send_runtime_byte(state, "ctlstpad.sock", 'H');
	pulse_workspace_haptic(state);
	return true;
}

static int
shell_bottom_bar_height(const struct daemon_state *state)
{
	return state->output.rect.width > state->output.rect.height ?
	    SHELL_LANDSCAPE_BOTTOM_BAR_HEIGHT : SHELL_BOTTOM_BAR_HEIGHT;
}

static int
keyboard_height(const struct daemon_state *state)
{
	if (!runtime_file_exists(state, "ctlstkeyboard.visible"))
		return 0;
	return state->output.rect.width > state->output.rect.height ?
	    KEYBOARD_LANDSCAPE_HEIGHT : KEYBOARD_PORTRAIT_HEIGHT;
}

static bool
glob_matches_casefold(const char *pattern, const char *text)
{
	while (*pattern != '\0') {
		if (*pattern == '*') {
			while (*pattern == '*')
				pattern++;
			if (*pattern == '\0')
				return true;
			while (*text != '\0') {
				if (glob_matches_casefold(pattern, text))
					return true;
				text++;
			}
			return false;
		}
		if (*text == '\0')
			return false;
		if (*pattern != '?' && tolower((unsigned char)*pattern) !=
		    tolower((unsigned char)*text))
			return false;
		pattern++;
		text++;
	}
	return *text == '\0';
}

static bool
default_right_click_exclusion(const char *app_id)
{
	static const char *patterns[] = {
		"dev.ctlst.*",
		"waydroid.*",
		"*camera*",
		"*dolphin*",
		"*gamescope*",
		"*retroarch*",
		"*steam*",
		"wvkbd*",
	};

	for (size_t index = 0; index < sizeof(patterns) / sizeof(patterns[0]);
	    index++) {
		if (glob_matches_casefold(patterns[index], app_id))
			return true;
	}
	return false;
}

static bool
right_click_app_excluded(const char *app_id)
{
	const char *config_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char path[512];
	char line[256];
	FILE *config;

	if (config_home != NULL && config_home[0] != '\0') {
		if (snprintf(path, sizeof(path), "%s/ctlst/right-click-exclude",
		    config_home) >= (int)sizeof(path))
			return default_right_click_exclusion(app_id);
	} else if (home != NULL && home[0] != '\0') {
		if (snprintf(path, sizeof(path),
		    "%s/.config/ctlst/right-click-exclude", home) >=
		    (int)sizeof(path))
			return default_right_click_exclusion(app_id);
	} else {
		return default_right_click_exclusion(app_id);
	}
	config = fopen(path, "r");
	if (config == NULL)
		return default_right_click_exclusion(app_id);
	while (fgets(line, sizeof(line), config) != NULL) {
		char *start = line;
		char *end;

		while (isspace((unsigned char)*start))
			start++;
		if (*start == '\0' || *start == '#')
			continue;
		end = start + strlen(start);
		while (end > start && isspace((unsigned char)end[-1]))
			*--end = '\0';
		if (*start != '\0' && glob_matches_casefold(start, app_id)) {
			fclose(config);
			return true;
		}
	}
	fclose(config);
	return false;
}

static void
cancel_right_click(struct daemon_state *state)
{
	state->right_click_deadline = 0;
	state->right_click_node_id = 0;
}

static bool
right_click_point_allowed(struct daemon_state *state, const struct node *node,
    struct point point)
{
	int output_bottom = state->output.rect.y + state->output.rect.height;
	int keyboard_top = output_bottom - keyboard_height(state);

	if (state->drawer_visible || state->shade_visible ||
	    state->overview_visible || state->dialer_visible ||
	    runtime_file_exists(state, "ctlstlock.visible") ||
	    runtime_file_exists(state, "ctlsthome.layout-editing") ||
	    runtime_file_exists(state, "ctlstpad.visible") ||
	    runtime_file_exists(state, "ctlstsession.visible"))
		return false;
	if (right_click_app_excluded(node->app_id))
		return false;
	if (point.x < node->rect.x ||
	    point.x > node->rect.x + node->rect.width ||
	    point.y < node->rect.y ||
	    point.y > node->rect.y + node->rect.height)
		return false;
	/* Keep compositor-owned bars, edge gestures, and the OSK out of scope. */
	if (point.x <= state->output.rect.x + BACK_EDGE_WIDTH ||
	    point.y < state->output.rect.y + SHELL_TOP_BAR_HEIGHT ||
	    point.y >= output_bottom - shell_bottom_bar_height(state) ||
	    (keyboard_height(state) > 0 && point.y >= keyboard_top))
		return false;
	return true;
}

static bool
arm_right_click(struct daemon_state *state, const struct node *node,
    struct point point)
{
	if (state->gesture.mode != GESTURE_NONE ||
	    !right_click_point_allowed(state, node, point))
		return false;
	state->right_click_deadline = monotonic_us() + RIGHT_CLICK_HOLD_US;
	state->right_click_node_id = node->id;
	state->right_click_point = point;
	return true;
}

static bool
arm_expired_right_click(struct daemon_state *state)
{
	struct node node;
	struct touch_slot *slot = NULL;
	struct point point = state->right_click_point;
	char command[192];
	int x;
	int y;

	if (state->right_click_deadline == 0 ||
	    state->right_click_deadline > monotonic_us())
		return false;
	state->right_click_deadline = 0;
	if (state->input.active_count != 1 ||
	    state->gesture.mode != GESTURE_NONE)
		return false;
	for (int index = 0; index < MAX_TOUCH_SLOTS; index++) {
		if (state->input.slots[index].active) {
			slot = &state->input.slots[index];
			break;
		}
	}
	if (slot == NULL || hypot(slot->x - slot->start_x,
	    slot->y - slot->start_y) /
	    (state->output.scale > 0 ? state->output.scale : 1.0) >
	    TOUCH_MOTION_SLOP ||
	    !load_tree_node(state, state->right_click_node_id, &node) ||
	    !right_click_point_allowed(state, &node, point)) {
		cancel_right_click(state);
		return false;
	}
	x = (int)lround(point.x);
	y = (int)lround(point.y);
	/* Sway treats zero as "leave this axis unchanged" for cursor set. */
	if (x == 0)
		x = 1;
	if (y == 0)
		y = 1;
	snprintf(command, sizeof(command),
	    "seat seat0 cursor set %d %d; seat seat0 cursor press button3; "
	    "seat seat0 cursor release button3", x, y);
	if (!run_command(state, command)) {
		cancel_right_click(state);
		return false;
	}
	reset_gesture(state);
	state->gesture.mode = GESTURE_CONSUMED;
	state->gesture.start = point;
	state->gesture.node_id = node.id;
	state->right_click_fired = true;
	block_shell_clicks(state);
	pulse_workspace_haptic(state);
	return true;
}

static bool
focused_surface_reserves_shell_bar(struct daemon_state *state)
{
	struct node node;

	return load_tree_node(state, 0, &node) &&
	    strcmp(node.app_id, "dev.ctlst.Camera") == 0;
}

static bool
workspace_selector_geometry_contains(const struct daemon_state *state,
    struct point point)
{
	double left = state->output.rect.x + state->output.rect.width *
	    WORKSPACE_WHEEL_LEFT_FRACTION;
	double right = state->output.rect.x + state->output.rect.width *
	    WORKSPACE_WHEEL_RIGHT_FRACTION;
	/* A few extra pixels above the shell bar so the strip is easier to hit. */
	int band = shell_bottom_bar_height(state) + 8;
	int bottom = state->output.rect.y + state->output.rect.height -
	    keyboard_height(state);
	int top = bottom - band;

	return point.x >= left && point.x <= right &&
	    point.y >= top && point.y <= bottom;
}

static bool
workspace_selector_contains(struct daemon_state *state, struct point point)
{
	if (state->drawer_visible || state->shade_visible ||
	    state->overview_visible || state->dialer_visible ||
	    runtime_file_exists(state, "ctlstlock.visible") ||
	    runtime_file_exists(state, "ctlsthome.layout-editing") ||
	    focused_surface_reserves_shell_bar(state))
		return false;
	return workspace_selector_geometry_contains(state, point);
}

static bool
fixed_workspace_selector_target(const struct daemon_state *state, double x,
    char *target, size_t target_size)
{
	double left = state->output.rect.x + state->output.rect.width / 2.0 -
	    WORKSPACE_SELECTOR_WIDTH / 2.0;
	int number = (int)((x - left) /
	    (WORKSPACE_SELECTOR_WIDTH /
	    (LAST_WORKSPACE - FIRST_WORKSPACE + 1))) + FIRST_WORKSPACE;

	if (number < FIRST_WORKSPACE)
		number = FIRST_WORKSPACE;
	if (number > LAST_WORKSPACE)
		number = LAST_WORKSPACE;
	snprintf(target, target_size, "%d", number);
	return true;
}

static bool __attribute__((unused))
workspace_selector_target_name(struct daemon_state *state, double x,
    char *target, size_t target_size)
{
	const struct workspace_list *list;
	double width = MAX_VISIBLE_DOCK_TASKS * DOCK_TASK_PITCH;
	double left = state->output.rect.x + state->output.rect.width / 2.0 -
	    width / 2.0;
	int focused_index;
	int slot;
	int index;

	list = cached_workspace_list(state);
	if (list == NULL)
		return fixed_workspace_selector_target(state, x, target,
		    target_size);
	if (list->count == 0)
		return false;
	focused_index = workspace_list_index(list, list->focused);
	if (focused_index < 0)
		focused_index = 0;
	slot = (int)((x - left) / DOCK_TASK_PITCH);
	if (slot < 0)
		slot = 0;
	if (slot >= MAX_VISIBLE_DOCK_TASKS)
		slot = MAX_VISIBLE_DOCK_TASKS - 1;
	index = (focused_index + (int)list->count +
	    slot - MAX_VISIBLE_DOCK_TASKS / 2) % (int)list->count;
	snprintf(target, target_size, "%s", list->names[index]);
	return true;
}

static bool
dynamic_app_target(struct daemon_state *state, int direction,
    char *target, size_t target_size)
{
	struct workspace_list list;
	int index;

	if (!workspace_manager_list(state, &list) || list.count <= 1)
		return false;
	index = workspace_list_index(&list, state->workspace_name);
	if (index < 1)
		index = direction > 0 ? 0 : (int)list.count;
	index += direction > 0 ? 1 : -1;
	if (index >= (int)list.count)
		index = 1;
	if (index < 1)
		index = (int)list.count - 1;
	snprintf(target, target_size, "%s", list.names[index]);
	return true;
}

static bool
move_window_to_new_task(struct daemon_state *state, int64_t node_id, bool focus,
    char *target, size_t target_size)
{
	char workspace[MAX_WORKSPACE_NAME];

	if (!workspace_manager_move(state, node_id, "new", focus, workspace,
	    sizeof(workspace)))
		return false;
	if (target != NULL && target_size > 0)
		snprintf(target, target_size, "%s", workspace);
	snprintf(state->workspace_name, sizeof(state->workspace_name), "%s",
	    workspace);
	state->workspace = 0;
	(void)refresh_workspace_cache(state);
	return true;
}

/*
 * Titlebar flick / horizontal task transfer. Matches the floating drop-card
 * policy: split a multi-window workspace onto a new task, otherwise move to an
 * adjacent app workspace — or create one when this is the only app task.
 */
static bool
move_titlebar_window_to_task(struct daemon_state *state, int64_t node_id,
    int direction)
{
	char target[MAX_WORKSPACE_NAME];
	char command[256];
	int windows;

	if (!refresh_workspace(state))
		return false;
	windows = focused_workspace_window_count(state);
	if (windows > 1) {
		return move_window_to_new_task(state, node_id, true, target,
		    sizeof(target));
	} else if (dynamic_app_target(state, direction, target,
	    sizeof(target))) {
		snprintf(command, sizeof(command),
		    "[con_id=%" PRId64 "] move container to workspace %.*s; "
		    "workspace %.*s", node_id, 48, target, 48, target);
		if (!run_command(state, command))
			return false;
	} else if (!workspace_manager_move(state, node_id, "new", true, target,
	    sizeof(target))) {
		return false;
	}
	snprintf(state->workspace_name, sizeof(state->workspace_name), "%.*s",
	    48, target);
	state->workspace = 0;
	(void)refresh_workspace_cache(state);
	return true;
}

static void
step_workspace_wheel(struct daemon_state *state, int direction)
{
	struct workspace_list *list = &state->gesture.wheel_list;
	const char *target;
	char command[192];
	int index;

	if (list->count == 0) {
		if (step_workspace(state, direction, false))
			pulse_workspace_haptic(state);
		return;
	}
	index = state->gesture.wheel_index;
	if (index < 0)
		index = 0;
	index = (index + (direction > 0 ? 1 : -1) +
	    (int)list->count) % (int)list->count;
	target = list->names[index];
	snprintf(command, sizeof(command), "workspace %.*s", 48, target);
	if (!run_command(state, command))
		return;
	state->gesture.wheel_index = index;
	snprintf(state->workspace_name, sizeof(state->workspace_name),
	    "%.*s", 48, target);
	snprintf(state->workspace_cache.focused,
	    sizeof(state->workspace_cache.focused), "%.*s", 48, target);
	state->workspace = numeric_workspace_name(target);
	state->last_workspace_step = monotonic_us();
	pulse_workspace_haptic(state);
}

static void
update_workspace_wheel(struct daemon_state *state, struct point point)
{
	double delta = point.x - state->gesture.wheel_anchor_x;

	while (fabs(delta) >= WORKSPACE_WHEEL_DETENT) {
		int direction = delta < 0 ? -1 : 1;

		step_workspace_wheel(state, direction);
		state->gesture.wheel_anchor_x +=
		    direction < 0 ? -WORKSPACE_WHEEL_DETENT :
		    WORKSPACE_WHEEL_DETENT;
		delta = point.x - state->gesture.wheel_anchor_x;
	}
}

static struct point
action_hub_center(const struct daemon_state *state)
{
	int bar_height = shell_bottom_bar_height(state);

	return (struct point){
		.x = state->output.rect.x + state->output.rect.width -
		    ACTION_HUB_RIGHT,
		.y = state->output.rect.y + state->output.rect.height -
		    keyboard_height(state) - bar_height / 2.0,
	};
}

static bool
action_hub_available(struct daemon_state *state, struct point point)
{
	struct point hub = action_hub_center(state);
	int bar_height = shell_bottom_bar_height(state);
	bool locked = runtime_file_exists(state, "ctlstlock.visible");

	if (state->debug)
		fprintf(stderr,
		    "action: hub %.1f,%.1f point %.1f,%.1f bar=%d locked=%d\n",
		    hub.x, hub.y, point.x, point.y, bar_height, locked);
	if (locked || focused_surface_reserves_shell_bar(state))
		return false;
	if (fabs(point.x - hub.x) > ACTION_HUB_RADIUS_X ||
	    fabs(point.y - hub.y) > bar_height / 2.0)
		return false;
	return true;
}

static char
action_target_code(enum action_target target)
{
	switch (target) {
	case ACTION_PAD:
		return 'P';
	case ACTION_KEYS:
		return 'K';
	case ACTION_CLOSE:
		return 'X';
	default:
		return 'N';
	}
}

static enum action_target
action_target_at(const struct daemon_state *state, struct point point)
{
	static const struct {
		enum action_target target;
		double offset_x;
		double offset_y;
	} items[] = {
		{ACTION_PAD, -118.0, -26.0},
		{ACTION_KEYS, -88.0, -75.0},
		{ACTION_CLOSE, -34.0, -108.0},
	};
	struct point hub = action_hub_center(state);
	enum action_target nearest = ACTION_NONE;
	double nearest_distance = ACTION_TARGET_RADIUS;

	if (hypot(point.x - hub.x, point.y - hub.y) < ACTION_CANCEL_RADIUS)
		return ACTION_NONE;
	for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
		double distance = hypot(point.x - (hub.x + items[i].offset_x),
		    point.y - (hub.y + items[i].offset_y));

		if (distance <= nearest_distance) {
			nearest = items[i].target;
			nearest_distance = distance;
		}
	}
	return nearest;
}

static void
update_action_orbit(struct daemon_state *state, struct point point)
{
	char message[64];
	enum action_target target = action_target_at(state, point);

	if (target != ACTION_NONE &&
	    target != state->gesture.action_target)
		pulse_workspace_haptic(state);
	state->gesture.action_target = target;
	if (target != ACTION_NONE)
		state->gesture.action_engaged = true;
	if (state->debug)
		fprintf(stderr, "action: target=%c x=%.1f y=%.1f\n",
		    action_target_code(target), point.x, point.y);
	snprintf(message, sizeof(message), "M %.1f %.1f %c",
	    point.x - state->output.rect.x,
	    point.y - state->output.rect.y, action_target_code(target));
	send_action_overlay_message(state, message);
}

static void
handle_down(struct daemon_state *state, const struct gesture_event *event)
{
	struct point start;
	struct point hub;
	struct node node;
	const struct workspace_list *list;
	char message[64];
	bool have_node;

	if (event->fingers != 1)
		return;
	state->touch_down_us = monotonic_us();
	start = transform_point(state, event->start_x, event->start_y,
	    event->screen_width, event->screen_height);
	/*
	 * Bottom-center strip: begin a slow inflate that only commits after the
	 * hold. Short taps pass through (morph collapses); drag/scrub is immediate;
	 * upward swipe goes Home.
	 */
	if (workspace_selector_contains(state, start)) {
		reset_gesture(state);
		state->gesture.mode = GESTURE_WORKSPACE_WHEEL_PENDING;
		state->gesture.start = start;
		state->gesture.wheel_anchor_x = start.x;
		state->gesture.wheel_dock_revealed = false;
		note_gesture_point(state, start);
		state->wheel_reveal_deadline =
		    state->touch_down_us + WORKSPACE_WHEEL_REVEAL_HOLD_US;
		/* Start at zero; poll/press drives P until the 200ms E commit. */
		send_dock_message(state, "P 0");
		/*
		 * Snapshot the task list at start so the scrub uses one coherent
		 * list for the whole gesture.
		 */
		refresh_workspace_cache(state);
		refresh_workspace(state);
		list = cached_workspace_list(state);
		if (list != NULL) {
			state->gesture.wheel_list = *list;
			state->gesture.wheel_index = workspace_list_index(
			    &state->gesture.wheel_list, state->workspace_name);
			if (state->gesture.wheel_index < 0)
				state->gesture.wheel_index = workspace_list_index(
				    &state->gesture.wheel_list, list->focused);
			if (state->debug)
				fprintf(stderr,
				    "workspace wheel count=%zu focused=%s sway=%s index=%d\n",
				    list->count, list->focused,
				    state->workspace_name,
				    state->gesture.wheel_index);
		}
		return;
	}
	have_node = load_tree_node(state, 0, &node);
	if (have_node && point_is_titlebar(&node, start)) {
		if (access(state->drop_overlay_path, F_OK) != 0)
			run_command(state, "exec env GSK_RENDERER=gl "
			    "$CTLST_LIBEXEC_DIR/"
			    "window-drop-overlay");
		reset_gesture(state);
		state->gesture.mode = GESTURE_WATCH;
		state->gesture.start = start;
		state->gesture.node_id = node.id;
		if (!node.floating)
			state->tiled_hold_deadline =
			    state->touch_down_us + WINDOW_DRAG_HOLD_US;
		return;
	}
	if (!action_hub_available(state, start)) {
		if (have_node)
			(void)arm_right_click(state, &node, start);
		return;
	}
	hub = action_hub_center(state);
	reset_gesture(state);
	snprintf(message, sizeof(message), "S %.1f %.1f %.1f %.1f",
	    hub.x - state->output.rect.x, hub.y - state->output.rect.y,
	    start.x - state->output.rect.x, start.y - state->output.rect.y);
	if (!send_action_overlay_message(state, message))
		return;
	state->gesture.mode = GESTURE_ACTION_ORBIT;
	state->gesture.start = start;
	state->gesture.action_target = ACTION_NONE;
	state->gesture.action_engaged = false;
	if (state->controls_revealed)
		state->controls_reveal_deadline = monotonic_us() + BAR_REVEAL_US;
	block_shell_clicks(state);
}

static void
finish_action_orbit(struct daemon_state *state, struct point end)
{
	enum action_target target = state->gesture.action_target;
	bool tap = !state->gesture.action_engaged &&
	    hypot(end.x - state->gesture.start.x,
	    end.y - state->gesture.start.y) <= TOUCH_MOTION_SLOP * 2.0;

	reset_gesture(state);
	if (state->debug)
		fprintf(stderr, "action: finish target=%c tap=%d\n",
		    action_target_code(target), tap);
	switch (target) {
	case ACTION_PAD:
		if (runtime_file_exists(state, "ctlstkeyboard.visible"))
			toggle_keyboard(state);
		run_command(state, "exec $CTLST_LIBEXEC_DIR/ctlstpad toggle");
		break;
	case ACTION_KEYS:
		if (runtime_file_exists(state, "ctlstpad.visible"))
			send_runtime_byte(state, "ctlstpad.sock", 'H');
		toggle_keyboard(state);
		break;
	case ACTION_CLOSE:
		close_focused_app(state);
		break;
	default:
		if (tap)
			toggle_keyboard(state);
		break;
	}
}

static bool
is_bottom_edge_start(const struct daemon_state *state, struct point start)
{
	int bar = shell_bottom_bar_height(state);

	return start.x >= state->output.rect.x &&
	    start.x <= state->output.rect.x + state->output.rect.width &&
	    start.y >= state->output.rect.y + state->output.rect.height - bar &&
	    start.y <= state->output.rect.y + state->output.rect.height;
}

static bool
is_bottom_action_swipe(const struct daemon_state *state, struct point start,
    struct point end)
{
	return is_bottom_edge_start(state, start) &&
	    bottom_pull_distance(start, end) >= BOTTOM_PULL_ACTION;
}

static bool
is_back_swipe(const struct daemon_state *state, struct point start,
    struct point end)
{
	double left = state->output.rect.x;
	double right = left + state->output.rect.width;
	double top = state->output.rect.y + SHELL_TOP_BAR_HEIGHT;
	/*
	 * ctlst-gestured reads the digitizer globally, so OSK presses are also
	 * candidates. Arrow keys sit on the right of the keyboard; a short slide
	 * while tapping was matching System Back (hide keyboard + haptic). Keep
	 * the back-edge band above the visible OSK exclusive zone.
	 */
	double bottom = state->output.rect.y + state->output.rect.height -
	    shell_bottom_bar_height(state) - keyboard_height(state);
	double dx = end.x - start.x;
	double dy = end.y - start.y;
	bool from_left = start.x >= left &&
	    start.x <= left + BACK_EDGE_WIDTH && dx >= BACK_SWIPE_DISTANCE;
	bool from_right = start.x <= right &&
	    start.x >= right - BACK_EDGE_WIDTH && dx <= -BACK_SWIPE_DISTANCE;

	if (bottom <= top)
		return false;
	return start.y >= top && start.y <= bottom &&
	    (from_left || from_right) && fabs(dy) <= fabs(dx) * 0.75;
}

static bool
surface_blocks_overview_pull(const struct daemon_state *state)
{
	/*
	 * Never open progressive overview while another shell surface owns the
	 * edge; recovery bottom-action still runs for those states.
	 */
	if (state->gesture.mode == GESTURE_ACTION_ORBIT)
		return true;
	return runtime_file_exists(state, "ctlstlock.visible") ||
	    runtime_file_exists(state, "ctlsthome.layout-editing") ||
	    runtime_file_exists(state, "ctlstkeyboard.visible") ||
	    runtime_file_exists(state, "ctlstdrawer.visible") ||
	    runtime_file_exists(state, "ctlstshade.visible") ||
	    runtime_file_exists(state, "ctlstoverview.visible") ||
	    runtime_file_exists(state, "ctlstdialer.visible") ||
	    runtime_file_exists(state, "ctlstpad.visible") ||
	    runtime_file_exists(state, "sway-touch-session-menu.pid");
}

static double
overview_pull_intent_distance(const struct daemon_state *state)
{
	double intent = state->output.rect.height * OVERVIEW_PULL_INTENT_FRACTION;

	if (intent < OVERVIEW_PULL_INTENT_DISTANCE)
		return OVERVIEW_PULL_INTENT_DISTANCE;
	return intent;
}

static bool
overview_pull_geometry_matches(const struct daemon_state *state,
    struct point start, struct point end)
{
	double distance = bottom_pull_distance(start, end);
	double dx = fabs(end.x - start.x);

	if (surface_blocks_overview_pull(state))
		return false;
	if (!is_bottom_edge_start(state, start))
		return false;
	/*
	 * The selector's pending mode resolves horizontal motion first. Once it
	 * releases a clearly vertical sequence, the same center start may open
	 * overview so the natural bottom-center thumb gesture works.
	 */
	if (distance < overview_pull_intent_distance(state))
		return false;
	if (dx > distance * 1.35)
		return false;
	return true;
}

static double
overview_pull_progress(const struct daemon_state *state, struct point start,
    struct point end)
{
	double distance = bottom_pull_distance(start, end);
	double intent = overview_pull_intent_distance(state);
	double full = state->output.rect.height * OVERVIEW_PULL_FULL_FRACTION;

	if (distance < intent)
		return 0.0;
	if (distance < 0.0)
		distance = 0.0;
	if (full <= intent + 1.0)
		return 1.0;
	if (distance >= full)
		return 1.0;
	return (distance - intent) / (full - intent);
}

static void
note_gesture_point(struct daemon_state *state, struct point point)
{
	state->gesture.last = point;
	state->gesture.last_us = monotonic_us();
}

static double
bottom_pull_overall_velocity(const struct daemon_state *state,
    struct point end)
{
	double distance = bottom_pull_distance(state->gesture.start, end);
	int64_t elapsed_us;

	if (distance < 0.0)
		distance = 0.0;
	elapsed_us = state->touch_down_us == 0 ? 0 :
	    monotonic_us() - state->touch_down_us;
	if (elapsed_us < 8000)
		elapsed_us = 8000;
	return distance * 1000000.0 / (double)elapsed_us;
}

static bool
bottom_pull_is_home_flick(const struct daemon_state *state, struct point end)
{
	double distance = bottom_pull_distance(state->gesture.start, end);
	int64_t elapsed_us;
	double height = (double)state->output.rect.height;
	double overall;

	if (distance < BOTTOM_PULL_ACTION)
		return false;
	elapsed_us = state->touch_down_us == 0 ? INT64_MAX :
	    monotonic_us() - state->touch_down_us;
	/* Short whole-gesture duration is the primary flick signal. */
	if (elapsed_us <= BOTTOM_PULL_HOME_FLICK_MAX_US)
		return true;
	if (height < 1.0)
		height = 1.0;
	overall = bottom_pull_overall_velocity(state, end);
	return overall >= height * BOTTOM_PULL_HOME_FLICK_HEIGHTS_S;
}

static void
update_overview_pull(struct daemon_state *state, struct point end)
{
	char message[32];
	double progress = overview_pull_progress(state, state->gesture.start,
	    end);

	note_gesture_point(state, end);
	if (progress <= 0.0)
		return;
	snprintf(message, sizeof(message), "P%.3f", progress);
	send_overview_message(state, message);
}

static bool
handle_overview_pull_press(struct daemon_state *state, struct point start,
    struct point end)
{
	if (state->gesture.mode == GESTURE_OVERVIEW_PULL) {
		if (!same_sequence(&state->gesture, start))
			return false;
		update_overview_pull(state, end);
		return true;
	}
	if (state->gesture.mode != GESTURE_NONE &&
	    state->gesture.mode != GESTURE_WATCH)
		return false;
	if (!overview_pull_geometry_matches(state, start, end))
		return false;
	reset_gesture(state);
	state->gesture.mode = GESTURE_OVERVIEW_PULL;
	state->gesture.start = start;
	note_gesture_point(state, end);
	block_shell_clicks(state);
	update_overview_pull(state, end);
	return true;
}

static void
finish_overview_pull(struct daemon_state *state, struct point end)
{
	double distance = bottom_pull_distance(state->gesture.start, end);
	double commit = state->output.rect.height * OVERVIEW_PULL_COMMIT_FRACTION;

	/*
	 * Speed splits the two upward actions: a quick flick goes Home, a slower
	 * deliberate pull commits multitasking overview.
	 */
	if (bottom_pull_is_home_flick(state, end)) {
		send_overview_message(state, "H");
		state->overview_visible = false;
		if (run_home_navigation(state))
			pulse_workspace_haptic(state);
		return;
	}
	if (distance >= commit) {
		send_overview_message(state, "S");
		state->overview_visible = true;
		return;
	}
	/* Slow short pull: cancel preview only. */
	send_overview_message(state, "H");
	state->overview_visible = false;
}

static bool
drawer_pull_geometry_matches(struct daemon_state *state, struct point start,
    struct point end)
{
	double left = state->output.rect.x + state->output.rect.width *
	    DRAWER_PULL_CENTER_MIN;
	double right = state->output.rect.x + state->output.rect.width *
	    DRAWER_PULL_CENTER_MAX;
	double distance = bottom_pull_distance(start, end);
	double dx = fabs(end.x - start.x);
	double bottom_limit = state->output.rect.y + state->output.rect.height -
	    state->drawer_pull_bottom_inset;
	double overview_limit = state->output.rect.y + state->output.rect.height -
	    shell_bottom_bar_height(state) - TOUCH_MOTION_SLOP;

	/* Never let drawer tuning steal the physical-bottom Home/overview band. */
	if (bottom_limit > overview_limit)
		bottom_limit = overview_limit;

	if (state->drawer_visible || state->shade_visible ||
	    state->overview_visible || state->dialer_visible ||
	    runtime_file_exists(state, "ctlsthome.layout-editing") ||
	    runtime_file_exists(state, "ctlstkeyboard.visible") ||
	    runtime_file_exists(state, "ctlstshade.visible") ||
	    runtime_file_exists(state, "ctlstoverview.visible") ||
	    runtime_file_exists(state, "ctlstdialer.visible") ||
	    runtime_file_exists(state, "ctlstpad.visible") ||
	    runtime_file_exists(state, "ctlstlock.visible") ||
	    runtime_file_exists(state, "sway-touch-session-menu.pid"))
		return false;
	return start.x >= left && start.x <= right &&
	    start.y >= state->output.rect.y + state->output.rect.height *
	    DRAWER_PULL_START_Y_MIN &&
	    start.y <= bottom_limit &&
	    distance >= DRAWER_PULL_INTENT_DISTANCE &&
	    distance >= dx * DRAWER_PULL_AXIS_RATIO;
}

static bool
drawer_pull_became_horizontal(struct point start, struct point end)
{
	double distance = bottom_pull_distance(start, end);
	double dx = fabs(end.x - start.x);

	return dx >= DRAWER_PULL_INTENT_DISTANCE &&
	    (distance <= 0.0 || dx > distance * DRAWER_PULL_AXIS_RATIO);
}

static bool
drawer_pull_workspace_available(struct daemon_state *state)
{
	int windows = focused_workspace_window_count(state);

	if (windows < 0)
		return false;
	return strcmp(state->workspace_name, "1") == 0 || windows == 0;
}

static bool
drawer_pull_start_allowed(struct daemon_state *state, struct point start,
    struct point end)
{
	return drawer_pull_geometry_matches(state, start, end) &&
	    drawer_pull_workspace_available(state);
}

static void
update_drawer_pull(struct daemon_state *state, struct point end)
{
	double distance = bottom_pull_distance(state->gesture.start, end);
	double reveal;
	int margin;

	if (distance < 0)
		distance = 0;
	reveal = distance * state->drawer_pull_tracking_gain;
	margin = (int)lround(state->output.rect.height - reveal);
	if (margin >= state->output.rect.height)
		margin = state->output.rect.height - 1;
	if (margin < 0)
		margin = 0;
	send_drawer_progress(state, margin);
}

static bool
handle_drawer_pull_press(struct daemon_state *state, struct point start,
    struct point end)
{
	if (state->gesture.mode == GESTURE_DRAWER_PULL) {
		if (!same_sequence(&state->gesture, start))
			return false;
		/* A tentative upward start may resolve into a Home-page swipe.
		 * Retract the preview immediately and keep it closed for the rest
		 * of this touch sequence. GTK's Home pager can continue normally. */
		if (drawer_pull_became_horizontal(start, end)) {
			send_drawer_command(state, 'H');
			state->drawer_visible = false;
			reset_gesture(state);
			state->gesture.mode = GESTURE_CONSUMED;
			state->gesture.start = start;
			return true;
		}
		update_drawer_pull(state, end);
		return true;
	}
	if (state->gesture.mode != GESTURE_NONE &&
	    state->gesture.mode != GESTURE_WATCH)
		return false;
	if (!drawer_pull_geometry_matches(state, start, end))
		return false;
	if (!drawer_pull_start_allowed(state, start, end)) {
		/*
		 * Once vertical intent is clear over an application workspace,
		 * leave the remaining sequence to the application without
		 * repeating Sway tree queries for every slider update.
		 */
		reset_gesture(state);
		state->gesture.mode = GESTURE_CONSUMED;
		state->gesture.start = start;
		return true;
	}
	reset_gesture(state);
	state->gesture.mode = GESTURE_DRAWER_PULL;
	state->gesture.start = start;
	block_shell_clicks(state);
	update_drawer_pull(state, end);
	return true;
}

static void
finish_drawer_pull(struct daemon_state *state, struct point end)
{
	double distance = bottom_pull_distance(state->gesture.start, end);

	if (distance >= state->drawer_pull_commit_distance) {
		send_drawer_command(state, 'S');
		state->drawer_visible = true;
	} else {
		send_drawer_command(state, 'H');
		state->drawer_visible = false;
	}
}

static bool
is_top_bar_pull(const struct daemon_state *state, struct point start,
    struct point end)
{
	return start.x >= state->output.rect.x &&
	    start.x <= state->output.rect.x + state->output.rect.width &&
	    start.y >= state->output.rect.y &&
	    start.y < state->output.rect.y + SHELL_TOP_BAR_HEIGHT &&
	    end.y - start.y >= 44;
}

static bool
surface_blocks_shade_pull(const struct daemon_state *state)
{
	if (state->gesture.mode == GESTURE_ACTION_ORBIT)
		return true;
	return state->shade_visible ||
	    runtime_file_exists(state, "ctlstshade.visible") ||
	    state->drawer_visible ||
	    runtime_file_exists(state, "ctlstdrawer.visible") ||
	    state->overview_visible ||
	    runtime_file_exists(state, "ctlstoverview.visible") ||
	    runtime_file_exists(state, "ctlsthome.layout-editing") ||
	    runtime_file_exists(state, "ctlstlock.visible") ||
	    runtime_file_exists(state, "ctlstkeyboard.visible") ||
	    runtime_file_exists(state, "ctlstdialer.visible") ||
	    runtime_file_exists(state, "ctlstpad.visible") ||
	    runtime_file_exists(state, "sway-touch-session-menu.pid");
}

static bool
shade_pull_home_available(const struct daemon_state *state)
{
	return strcmp(state->workspace_name, "1") == 0 &&
	    runtime_file_exists(state, "ctlsthome.visible");
}

static bool
shade_pull_top_bar_matches(const struct daemon_state *state,
    struct point start, struct point end)
{
	double distance = end.y - start.y;
	double dx = fabs(end.x - start.x);

	if (start.x < state->output.rect.x ||
	    start.x > state->output.rect.x + state->output.rect.width)
		return false;
	if (start.y < state->output.rect.y ||
	    start.y >= state->output.rect.y + SHELL_TOP_BAR_HEIGHT)
		return false;
	if (distance < SHADE_PULL_INTENT_DISTANCE)
		return false;
	if (dx > distance * 1.35)
		return false;
	return true;
}

static bool
shade_pull_content_zone_matches(const struct daemon_state *state,
    struct point start, struct point end)
{
	double depth = state->shade_home_pull_start_depth;
	double bottom;
	double distance = end.y - start.y;
	double dx = fabs(end.x - start.x);

	/* Values through 1.0 are output-height fractions; larger values are
	 * absolute logical pixels. This keeps 0.40 rotation-independent while
	 * still allowing exact per-device tuning such as 384. */
	if (depth <= 1.0)
		depth *= state->output.rect.height;
	if (depth > state->output.rect.height)
		depth = state->output.rect.height;
	bottom = state->output.rect.y + depth;
	if (start.x < state->output.rect.x ||
	    start.x > state->output.rect.x + state->output.rect.width)
		return false;
	if (start.y < state->output.rect.y)
		return false;
	if (start.y > bottom)
		return false;
	if (distance < SHADE_PULL_INTENT_DISTANCE)
		return false;
	if (dx > distance * 1.35)
		return false;
	return true;
}

static bool
shade_pull_from_home_content_zone(const struct daemon_state *state,
    struct point start, struct point end)
{
	return shade_pull_content_zone_matches(state, start, end);
}

static bool
shade_pull_start_allowed(struct daemon_state *state, struct point start,
    struct point end)
{
	if (shade_pull_top_bar_matches(state, start, end))
		return true;
	return shade_pull_home_available(state) &&
	    shade_pull_from_home_content_zone(state, start, end);
}

static bool
shade_pull_geometry_matches(const struct daemon_state *state,
    struct point start, struct point end)
{
	if (surface_blocks_shade_pull(state))
		return false;
	if (shade_pull_top_bar_matches(state, start, end))
		return true;
	return shade_pull_home_available(state) &&
	    shade_pull_content_zone_matches(state, start, end);
}

static double
shade_pull_progress(const struct daemon_state *state, struct point start,
    struct point end)
{
	double distance = end.y - start.y;
	double full = state->output.rect.height - SHADE_BOTTOM_MARGIN;

	if (distance < 0.0)
		distance = 0.0;
	distance *= state->shade_pull_tracking_gain;
	if (full <= 1.0)
		return 1.0;
	if (distance >= full)
		return 1.0;
	return distance / full;
}

static void
update_shade_pull(struct daemon_state *state, struct point end)
{
	char message[32];
	double progress = shade_pull_progress(state, state->gesture.start, end);

	snprintf(message, sizeof(message), "P%.3f", progress);
	send_shade_message(state, message);
}

static bool
handle_shade_pull_press(struct daemon_state *state, struct point start,
    struct point end)
{
	if (state->gesture.mode == GESTURE_SHADE_PULL) {
		if (!same_sequence(&state->gesture, start))
			return false;
		update_shade_pull(state, end);
		return true;
	}
	if (state->gesture.mode != GESTURE_NONE &&
	    state->gesture.mode != GESTURE_WATCH)
		return false;
	if (!shade_pull_geometry_matches(state, start, end))
		return false;
	if (!shade_pull_start_allowed(state, start, end)) {
		reset_gesture(state);
		state->gesture.mode = GESTURE_CONSUMED;
		state->gesture.start = start;
		return true;
	}
	if (state->drawer_visible)
		send_drawer_command(state, 'H');
	if (state->overview_visible)
		send_overview_message(state, "H");
	state->drawer_visible = false;
	state->overview_visible = false;
	reset_gesture(state);
	state->gesture.mode = GESTURE_SHADE_PULL;
	state->gesture.start = start;
	block_shell_clicks(state);
	update_shade_pull(state, end);
	return true;
}

static void
finish_shade_pull(struct daemon_state *state, struct point end)
{
	double distance = end.y - state->gesture.start.y;

	if (distance >= state->shade_pull_commit_distance) {
		send_shade_command(state, 'S');
		state->shade_visible = true;
		return;
	}
	send_shade_command(state, 'H');
	state->shade_visible = false;
}

static bool
is_settings_pull(const struct daemon_state *state, struct point start,
    struct point end)
{
	return is_top_bar_pull(state, start, end);
}

static bool
handle_top_pull(struct daemon_state *state, struct point start,
    struct point end)
{
	struct node node;
	char command[128];

	if (!is_top_bar_pull(state, start, end))
		return false;
	block_shell_clicks(state);
	if (is_settings_pull(state, start, end)) {
		if (state->drawer_visible)
			send_drawer_command(state, 'H');
		if (state->overview_visible)
			send_overview_message(state, "H");
		state->drawer_visible = false;
		state->overview_visible = false;
		if (!runtime_file_exists(state, "ctlstshade.visible")) {
			state->shade_visible = false;
			send_shade_command(state, 'S');
		} else {
			state->shade_visible = true;
		}
		return true;
	}
	if (load_tree_node(state, 0, &node) && node.fullscreen != 0) {
		snprintf(command, sizeof(command),
		    "[con_id=%" PRId64 "] fullscreen disable", node.id);
		run_command(state, command);
	}
	return true;
}

static void
handle_press(struct daemon_state *state, const struct gesture_event *event)
{
	struct point start;
	struct point end;
	struct node node;

	if (event->fingers != 1)
		return;
	start = transform_point(state, event->start_x, event->start_y,
	    event->screen_width, event->screen_height);
	end = transform_point(state, event->end_x, event->end_y,
	    event->screen_width, event->screen_height);
	if (state->debug) {
		fprintf(stderr, "gesture P fingers=%d %.1f,%.1f -> %.1f,%.1f "
		    "top=%d settings=%d bottom=%d drawer=%d shade=%d "
		    "overview=%d mode=%d\n",
		    event->fingers, start.x, start.y, end.x, end.y,
		    is_top_bar_pull(state, start, end),
		    is_settings_pull(state, start, end),
		    is_bottom_action_swipe(state, start, end),
		    state->drawer_visible, state->shade_visible, state->overview_visible,
		    state->gesture.mode);
	}
	if (state->gesture.mode == GESTURE_CONSUMED)
		return;
	if (state->gesture.mode == GESTURE_ACTION_ORBIT) {
		if (same_sequence(&state->gesture, start))
			update_action_orbit(state, end);
		return;
	}
	if (state->gesture.mode == GESTURE_WORKSPACE_WHEEL) {
		if (same_sequence(&state->gesture, start)) {
			block_shell_clicks(state);
			send_dock_message(state, "E");
			update_workspace_wheel(state, end);
		}
		return;
	}
	if (state->gesture.mode == GESTURE_WORKSPACE_WHEEL_PENDING) {
		double dx = end.x - start.x;
		double dy = end.y - start.y;
		int64_t held_us = state->touch_down_us == 0 ? 0 :
		    monotonic_us() - state->touch_down_us;

		if (!same_sequence(&state->gesture, start)) {
			reset_gesture(state);
		} else if (fabs(dx) >= WORKSPACE_WHEEL_DETENT &&
		    fabs(dx) > fabs(dy)) {
			/* Horizontal slide: scrub workspaces and reveal the dock. */
			state->gesture.mode = GESTURE_WORKSPACE_WHEEL;
			state->wheel_reveal_deadline = 0;
			reveal_workspace_wheel_dock(state);
			update_workspace_wheel(state, end);
			return;
		} else if (held_us >= WORKSPACE_WHEEL_REVEAL_HOLD_US &&
		    fabs(dx) < WORKSPACE_WHEEL_DETENT) {
			/*
			 * Hold on the home dash commits the task pill. Allow normal
			 * finger jitter; only a clear horizontal detent scrubs.
			 */
			reveal_workspace_wheel_dock(state);
			return;
		} else if (fabs(dy) <= WORKSPACE_WHEEL_DETENT ||
		    fabs(dy) <= fabs(dx)) {
			/* Waiting for detent / hold reveal / Home swipe. */
			note_gesture_point(state, end);
			update_workspace_wheel_hold_inflate(state);
			return;
		} else if (dy < 0 &&
		    !state->gesture.wheel_dock_revealed &&
		    overview_pull_geometry_matches(state, start, end)) {
			/* Clear upward pull from the dash → progressive overview. */
			reset_gesture(state);
			state->gesture.mode = GESTURE_OVERVIEW_PULL;
			state->gesture.start = start;
			note_gesture_point(state, end);
			block_shell_clicks(state);
			update_overview_pull(state, end);
			return;
		} else if (dy < 0 &&
		    drawer_pull_start_allowed(state, start, end)) {
			reset_gesture(state);
			state->gesture.mode = GESTURE_DRAWER_PULL;
			state->gesture.start = start;
			block_shell_clicks(state);
			update_drawer_pull(state, end);
			return;
		} else {
			/* Keep PENDING through vertical motion until release. */
			note_gesture_point(state, end);
			update_workspace_wheel_hold_inflate(state);
			return;
		}
	}
	if (is_back_swipe(state, start, end) && run_back_navigation(state)) {
		block_shell_clicks(state);
		reset_gesture(state);
		state->gesture.mode = GESTURE_CONSUMED;
		return;
	}
	/*
	 * Bottom-edge progressive overview claims the sequence before the
	 * immediate Home swipe, so short Home navigation still works on
	 * release below the commit threshold.
	 */
	if (handle_overview_pull_press(state, start, end))
		return;
	if (handle_drawer_pull_press(state, start, end))
		return;
	if (handle_shade_pull_press(state, start, end))
		return;
	/* Immediate top pull fallback when progressive shade is blocked. */
	if (handle_top_pull(state, start, end)) {
		reset_gesture(state);
		state->gesture.mode = GESTURE_CONSUMED;
		return;
	}
	/* Immediate Home navigation only when progressive overview is blocked. */
	if (is_bottom_action_swipe(state, start, end) &&
	    surface_blocks_overview_pull(state)) {
		block_shell_clicks(state);
		/* Editing owns Home, not the focused app's generic Back action. */
		if (runtime_file_exists(state, "ctlsthome.layout-editing") ||
		    !run_back_navigation(state))
			run_home_navigation(state);
		reset_gesture(state);
		state->gesture.mode = GESTURE_CONSUMED;
		return;
	}
	if (state->overview_visible || state->shade_visible ||
	    state->drawer_visible || state->dialer_visible) {
		reset_gesture(state);
		return;
	}
	if (state->drawer_close_sequence) {
		reset_gesture(state);
		return;
	}

	if (!same_sequence(&state->gesture, start)) {
		reset_gesture(state);
		state->gesture.start = start;
		state->gesture.mode = GESTURE_WATCH;
		if (!load_tree_node(state, 0, &node))
			return;
		state->gesture.node_id = node.id;
		if (try_fullscreen_motion(state, &node, start, end)) {
			block_shell_clicks(state);
			state->gesture.mode = GESTURE_CONSUMED;
			return;
		}
		if (begin_drag(state, &node, start, end)) {
			block_shell_clicks(state);
			return;
		}
		if (begin_resize(state, &node, start)) {
			block_shell_clicks(state);
			return;
		}
	}

	/*
	 * A horizontal touch may enter watch mode before its hold matures.
	 * Retry the drag claim so it can become window movement after the hold.
	 */
	if (state->gesture.mode == GESTURE_WATCH &&
	    load_tree_node(state, state->gesture.node_id, &node)) {
		if (try_fullscreen_motion(state, &node, start, end)) {
			block_shell_clicks(state);
			state->gesture.mode = GESTURE_CONSUMED;
			return;
		}
		if (begin_drag(state, &node, start, end)) {
			block_shell_clicks(state);
			update_drag(state, end);
			return;
		}
	}

	switch (state->gesture.mode) {
	case GESTURE_DRAG:
		update_drag(state, end);
		break;
	case GESTURE_TILED_DRAG:
		/* Sway owns movement; the daemon only tracks the explicit escape target. */
		update_drop_overlay(state, drop_target_at(state, end), end);
		break;
	case GESTURE_RESIZE:
		update_resize(state, end, false);
		break;
	default:
		break;
	}
}

static void
handle_titlebar_tap(struct daemon_state *state, struct point point)
{
	struct node node;
	int64_t now;
	int dx;
	int dy;
	char command[128];

	if (!load_tree_node(state, 0, &node) ||
	    !point_is_titlebar(&node, point))
		return;
	if (point_is_titlebar_corner(&node, point)) {
		if (node.floating)
			set_floating(state, node.id, false, false);
		else
			set_compact_floating(state, node.id);
		state->tap_node_id = 0;
		state->tap_time = 0;
		return;
	}
	now = monotonic_us();
	dx = (int)point.x - state->tap_x;
	dy = (int)point.y - state->tap_y;
	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	if (state->tap_node_id == node.id &&
	    now - state->tap_time >= 0 &&
	    now - state->tap_time <= TAP_WINDOW_US &&
	    dx <= 48 && dy <= 32) {
		snprintf(command, sizeof(command),
		    "[con_id=%" PRId64 "] fullscreen enable", node.id);
		run_command(state, command);
		state->tap_node_id = 0;
		state->tap_time = 0;
		return;
	}
	state->tap_node_id = node.id;
	state->tap_time = now;
	state->tap_x = (int)point.x;
	state->tap_y = (int)point.y;
}

static void
handle_one_finger_release(struct daemon_state *state,
    struct point start, struct point end)
{
	struct node node;
	double dx = end.x - start.x;
	double dy = end.y - start.y;

	if (state->gesture.mode == GESTURE_CONSUMED) {
		reset_gesture(state);
		return;
	}
	if (state->gesture.mode == GESTURE_DRAG ||
	    state->gesture.mode == GESTURE_SNAPPED) {
		finish_drag(state, end);
		reset_gesture(state);
		return;
	}
	if (state->gesture.mode == GESTURE_TILED_DRAG) {
		enum drop_target target = drop_target_at(state, end);

		if (target == DROP_NEW_TASK) {
			(void)move_window_to_new_task(state, state->gesture.node_id, true,
			    NULL, 0);
			reset_gesture(state);
			return;
		}
		if (target == DROP_WORKSPACE_DIRECT) {
			(void)move_window_to_existing_task(state,
			    state->gesture.node_id,
			    drop_workspace_index_at(state, end), false, true);
			reset_gesture(state);
			return;
		}
		/*
		 * Hold armed native tiling drag. A clearly horizontal swipe
		 * still means "send this window to another task" rather than
		 * abandoning the gesture after rearranging the split.
		 */
		if (fabs(dx) >= fabs(dy) &&
		    fabs(dx) >= state->output.rect.width *
		    TITLEBAR_SWIPE_COMMIT_FRACTION &&
		    load_tree_node(state, state->gesture.node_id, &node))
			(void)move_titlebar_window_to_task(state, node.id,
			    dx > 0 ? 1 : -1);
		reset_gesture(state);
		return;
	}
	if (state->gesture.mode == GESTURE_RESIZE) {
		update_resize(state, end, true);
		reset_gesture(state);
		return;
	}
	if (fabs(dx) <= 35 && fabs(dy) <= 35) {
		handle_titlebar_tap(state, start);
		reset_gesture(state);
		return;
	}
	if (!load_tree_node(state, state->gesture.node_id != 0 ?
	    state->gesture.node_id : 0, &node)) {
		reset_gesture(state);
		return;
	}
	if (try_fullscreen_motion(state, &node, start, end)) {
		reset_gesture(state);
		return;
	}
	if (fabs(dx) >= fabs(dy) && point_is_titlebar(&node, start)) {
		int direction = dx > 0 ? 1 : -1;
		int64_t held_us = state->touch_down_us == 0 ? INT64_MAX :
		    monotonic_us() - state->touch_down_us;
		double commit = state->output.rect.width *
		    TITLEBAR_SWIPE_COMMIT_FRACTION;

		/*
		 * Quick flicks always transfer. Longer holds still transfer
		 * when the finger travels far enough horizontally — otherwise
		 * floating reposition / tiled rearrange owns the gesture.
		 */
		if (held_us > TITLEBAR_SWIPE_MAX_US && fabs(dx) < commit) {
			reset_gesture(state);
			return;
		}

		(void)move_titlebar_window_to_task(state, node.id, direction);
	}
	reset_gesture(state);
}

static void
handle_multifinger_release(struct daemon_state *state,
    const struct gesture_event *event, struct point start, struct point end)
{
	double dx = end.x - start.x;
	double dy = end.y - start.y;

	if (event->fingers == 3) {
		if (fabs(dx) >= fabs(dy)) {
			step_workspace(state, dx < 0 ? 1 : -1, false);
		} else if (dy < 0) {
			/* Compatibility alias: primary open is one-finger bottom pull. */
			if (!surface_blocks_overview_pull(state) ||
			    state->overview_visible) {
				send_overview_message(state, "S");
				state->overview_visible = true;
			}
		} else {
			toggle_keyboard(state);
		}
		return;
	}
	if (event->fingers != 4)
		return;
	if (fabs(dx) >= fabs(dy)) {
		step_workspace(state, dx < 0 ? 1 : -1, true);
	} else if (dy < 0) {
		toggle_floating(state);
	} else {
		toggle_fullscreen(state);
	}
}

static void
handle_release(struct daemon_state *state, const struct gesture_event *event)
{
	struct point start;
	struct point end;

	start = transform_point(state, event->start_x, event->start_y,
	    event->screen_width, event->screen_height);
	end = transform_point(state, event->end_x, event->end_y,
	    event->screen_width, event->screen_height);
	if (state->gesture.mode == GESTURE_ACTION_ORBIT) {
		if (event->fingers == 1 &&
		    same_sequence(&state->gesture, start)) {
			record_touch_release(state, start, end);
			block_shell_clicks(state);
			update_action_orbit(state, end);
			finish_action_orbit(state, end);
		} else {
			reset_gesture(state);
		}
		return;
	}
	if (event->fingers == 1 &&
	    state->gesture.mode == GESTURE_WORKSPACE_WHEEL) {
		if (same_sequence(&state->gesture, start)) {
			record_touch_release(state, start, end);
			block_shell_clicks(state);
			update_workspace_wheel(state, end);
		}
		reset_gesture(state);
		return;
	}
	if (event->fingers == 1 &&
	    state->gesture.mode == GESTURE_WORKSPACE_WHEEL_PENDING) {
		/*
		 * Short tap under the dash passes through. Quick upward flick
		 * goes Home; a slower long pull that never claimed overview can
		 * still open multitasking. Hold/scrub that revealed the pill
		 * just collapses.
		 */
		if (same_sequence(&state->gesture, start) &&
		    !state->gesture.wheel_dock_revealed) {
			double distance = bottom_pull_distance(start, end);
			double dx = fabs(end.x - start.x);
			double commit = state->output.rect.height *
			    OVERVIEW_PULL_COMMIT_FRACTION;

			if (distance > dx && bottom_pull_is_home_flick(state, end)) {
				block_shell_clicks(state);
				if (run_home_navigation(state))
					pulse_workspace_haptic(state);
			} else if (distance >= commit && distance > dx &&
			    !surface_blocks_overview_pull(state)) {
				block_shell_clicks(state);
				send_overview_message(state, "S");
				state->overview_visible = true;
			}
		}
		reset_gesture(state);
		return;
	}
	if (event->fingers == 1 && !state->right_click_fired)
		record_touch_release(state, start, end);
	if (event->fingers == 1 &&
	    state->gesture.mode == GESTURE_CONSUMED) {
		reset_gesture(state);
		return;
	}
	if (event->fingers == 1 && is_back_swipe(state, start, end) &&
	    run_back_navigation(state)) {
		block_shell_clicks(state);
		reset_gesture(state);
		return;
	}
	if (event->fingers == 1 &&
	    state->gesture.mode == GESTURE_OVERVIEW_PULL) {
		if (same_sequence(&state->gesture, start)) {
			block_shell_clicks(state);
			finish_overview_pull(state, end);
		} else if (!state->overview_visible) {
			send_overview_message(state, "H");
		}
		reset_gesture(state);
		return;
	}
	if (event->fingers == 1 &&
	    state->gesture.mode == GESTURE_DRAWER_PULL) {
		finish_drawer_pull(state, end);
		reset_gesture(state);
		return;
	}
	if (event->fingers == 1 &&
	    state->gesture.mode == GESTURE_SHADE_PULL) {
		finish_shade_pull(state, end);
		reset_gesture(state);
		return;
	}
	if (event->fingers == 1 && handle_top_pull(state, start, end)) {
		reset_gesture(state);
		return;
	}
	if (event->fingers == 1 &&
	    is_bottom_action_swipe(state, start, end)) {
		/*
		 * When progressive overview owns the edge, release is handled
		 * by GESTURE_OVERVIEW_PULL. This path remains for Home when
		 * another surface blocks overview open.
		 */
		block_shell_clicks(state);
		/* Also handle fast swipes delivered only at release. */
		if (runtime_file_exists(state, "ctlsthome.layout-editing") ||
		    !run_back_navigation(state))
			run_home_navigation(state);
		reset_gesture(state);
		return;
	}
	if (state->overview_visible) {
		reset_gesture(state);
		return;
	}
	if (state->shade_visible) {
		reset_gesture(state);
		return;
	}
	if (state->drawer_visible) {
		reset_gesture(state);
		return;
	}
	if (event->fingers == 1) {
		if (hypot(end.x - start.x, end.y - start.y) <= 35.0)
			notify_editable_tap(state, end);
		handle_one_finger_release(state, start, end);
	} else {
		block_shell_clicks(state);
		handle_multifinger_release(state, event, start, end);
		reset_gesture(state);
	}
}

static bool
parse_event(const char *message, struct gesture_event *event)
{
	return sscanf(message, "e %c %d %lf %lf %lf %lf %d %d",
	    &event->phase, &event->fingers,
	    &event->start_x, &event->start_y,
	    &event->end_x, &event->end_y,
	    &event->screen_width, &event->screen_height) == 8 &&
	    (event->phase == 'D' || event->phase == 'P' ||
	    event->phase == 'R') &&
	    event->fingers >= 1 && event->fingers <= 10 &&
	    event->screen_width > 0 && event->screen_height > 0;
}

static void
reset_external_state(struct daemon_state *state)
{
	if (state->gesture.mode != GESTURE_CONSUMED &&
	    state->gesture.mode != GESTURE_DRAWER_PULL &&
	    state->gesture.mode != GESTURE_OVERVIEW_PULL &&
	    state->gesture.mode != GESTURE_SHADE_PULL)
		reset_gesture(state);
}

static bool
update_workspace_cache_message(struct daemon_state *state,
    const char *message, ssize_t size)
{
	struct workspace_list list = {0};
	char buffer[CONTROL_MESSAGE_SIZE];
	char *save = NULL;
	char *token;

	if (size < 5 || size >= (ssize_t)sizeof(buffer) ||
	    message[0] != 'l' || message[1] != ' ')
		return false;
	memcpy(buffer, message + 2, (size_t)size - 2);
	buffer[size - 2] = '\0';
	token = strtok_r(buffer, " \t\r\n", &save);
	if (token == NULL || !safe_workspace_name(token))
		return false;
	snprintf(list.focused, sizeof(list.focused), "%s", token);
	token = strtok_r(NULL, " \t\r\n", &save);
	if (token == NULL || !safe_workspace_name(token))
		return false;
	snprintf(list.home, sizeof(list.home), "%s", token);
	if (!workspace_list_add(&list, list.home))
		return false;
	while ((token = strtok_r(NULL, " \t\r\n", &save)) != NULL) {
		if (!workspace_list_add(&list, token))
			return false;
	}
	if (workspace_list_index(&list, list.focused) < 0)
		return false;
	state->workspace_cache = list;
	state->workspace_cache_valid = true;
	if (state->gesture.mode != GESTURE_WORKSPACE_WHEEL_PENDING &&
	    state->gesture.mode != GESTURE_WORKSPACE_WHEEL) {
		snprintf(state->workspace_name, sizeof(state->workspace_name),
		    "%s", list.focused);
		state->workspace = numeric_workspace_name(list.focused);
	}
	return true;
}

static void
handle_control_message(struct daemon_state *state, const char *message,
    ssize_t size)
{
	struct gesture_event event;

	if (update_workspace_cache_message(state, message, size))
		return;
	if (size == 2 && (message[0] == 'w' || message[0] == 'W') &&
	    message[1] >= '1' && message[1] <= '5') {
		select_workspace(state, message[1] - '0', message[0] == 'W');
		return;
	}
	if (size == 1) {
		switch (message[0]) {
		case 'n':
			step_workspace(state, 1, false);
			return;
		case 'p':
			step_workspace(state, -1, false);
			return;
		case 'N':
			step_workspace(state, 1, true);
			return;
		case 'P':
			step_workspace(state, -1, true);
			return;
		case 'v':
			state->drawer_visible = true;
			reset_external_state(state);
			return;
		case 'h':
			state->drawer_visible = false;
			reset_external_state(state);
			return;
		case 's':
			state->shade_visible = true;
			reset_external_state(state);
			return;
		case 'q':
			state->shade_visible = false;
			reset_external_state(state);
			return;
		case 'o':
			state->overview_visible = true;
			reset_external_state(state);
			return;
		case 'c':
			state->overview_visible = false;
			reset_external_state(state);
			return;
		case 'm':
			go_home(state);
			reset_external_state(state);
			return;
		case 'd':
			state->dialer_visible = true;
			reset_external_state(state);
			return;
		case 'e':
			state->dialer_visible = false;
			reset_external_state(state);
			return;
		default:
			return;
		}
	}
	if (!parse_event(message, &event))
		return;
	if (event.phase == 'D') {
		refresh_output(state);
		handle_down(state, &event);
	} else if (event.phase == 'P') {
		handle_press(state, &event);
	} else {
		handle_release(state, &event);
	}
}

static int
acquire_singleton_lock(struct daemon_state *state)
{
	char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	int fd;
	char record[32];
	int length;

	if (snprintf(path, sizeof(path), "%s/ctlst-gestured.lock",
	    state->runtime_dir) >= (int)sizeof(path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0)
		return -1;
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		close(fd);
		errno = EALREADY;
		return -1;
	}
	if (ftruncate(fd, 0) == 0) {
		length = snprintf(record, sizeof(record), "%ld\n",
		    (long)getpid());
		if (length > 0)
			(void)write_full(fd, record, (size_t)length);
	}
	state->lock_fd = fd;
	return 0;
}

static int
create_control_socket(struct daemon_state *state)
{
	struct sockaddr_un address = {0};
	int fd;

	if (snprintf(state->control_path, sizeof(state->control_path),
	    "%s/ctlst-gestures.sock", state->runtime_dir) >=
	    (int)sizeof(state->control_path) ||
	    snprintf(state->drawer_path, sizeof(state->drawer_path),
	    "%s/ctlst-drawer.sock", state->runtime_dir) >=
	    (int)sizeof(state->drawer_path) ||
	    snprintf(state->dock_path, sizeof(state->dock_path),
	    "%s/ctlst-dock.sock", state->runtime_dir) >=
	    (int)sizeof(state->dock_path) ||
	    snprintf(state->shade_path, sizeof(state->shade_path),
	    "%s/ctlst-shade.sock", state->runtime_dir) >=
	    (int)sizeof(state->shade_path) ||
	    snprintf(state->overview_path, sizeof(state->overview_path),
	    "%s/ctlst-overview.sock", state->runtime_dir) >=
	    (int)sizeof(state->overview_path) ||
	    snprintf(state->drop_overlay_path,
	    sizeof(state->drop_overlay_path),
	    "%s/ctlst-window-drop.sock", state->runtime_dir) >=
	    (int)sizeof(state->drop_overlay_path) ||
	    snprintf(state->action_overlay_path,
	    sizeof(state->action_overlay_path),
	    "%s/ctlst-action.sock", state->runtime_dir) >=
	    (int)sizeof(state->action_overlay_path) ||
	    snprintf(state->touch_state_path, sizeof(state->touch_state_path),
	    "%s/ctlst-last-touch", state->runtime_dir) >=
	    (int)sizeof(state->touch_state_path) ||
	    snprintf(state->click_block_path, sizeof(state->click_block_path),
	    "%s/ctlst-gesture-block-until", state->runtime_dir) >=
	    (int)sizeof(state->click_block_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return -1;
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, state->control_path);
	unlink(state->control_path);
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		close(fd);
		return -1;
	}
	chmod(state->control_path, 0600);
	return fd;
}

int
main(void)
{
	struct daemon_state state = {0};
	struct sigaction action = {0};
	struct sigaction ignore_action = {0};
	struct pollfd descriptors[2];
	char visible_path[256];
	int64_t input_retry_deadline = 0;
	int exit_status = EXIT_FAILURE;

	state.runtime_dir = getenv("XDG_RUNTIME_DIR");
	state.sway_socket = getenv("SWAYSOCK");
	state.debug = getenv("CTLST_GESTURE_DEBUG") != NULL;
	state.drawer_pull_bottom_inset = configured_double(
	    "CTLST_DRAWER_PULL_BOTTOM_INSET", DRAWER_PULL_BOTTOM_INSET, 0.0,
	    1024.0);
	state.drawer_pull_tracking_gain = configured_double(
	    "CTLST_DRAWER_PULL_TRACKING_GAIN", DRAWER_PULL_TRACKING_GAIN, 0.25,
	    4.0);
	state.drawer_pull_commit_distance = configured_double(
	    "CTLST_DRAWER_PULL_COMMIT_DISTANCE", DRAWER_PULL_COMMIT_DISTANCE,
	    16.0, 1024.0);
	state.shade_pull_tracking_gain = configured_double(
	    "CTLST_SHADE_PULL_TRACKING_GAIN", SHADE_PULL_TRACKING_GAIN, 0.25,
	    4.0);
	state.shade_pull_commit_distance = configured_double(
	    "CTLST_SHADE_PULL_COMMIT_DISTANCE", SHADE_PULL_COMMIT_DISTANCE,
	    16.0, 1024.0);
	state.shade_home_pull_start_depth = configured_double(
	    "CTLST_SHADE_HOME_PULL_START_DEPTH", SHADE_HOME_PULL_START_DEPTH,
	    0.0, 4096.0);
	state.command_fd = -1;
	state.control_fd = -1;
	state.lock_fd = -1;
	state.drawer_fd = -1;
	state.haptic_fd = -1;
	state.haptic_effect_id = -1;
	state.input.fd = -1;
	ignore_action.sa_handler = SIG_IGN;
	sigemptyset(&ignore_action.sa_mask);
	sigaction(SIGPIPE, &ignore_action, NULL);
	if (state.runtime_dir == NULL || state.sway_socket == NULL) {
		fprintf(stderr, "ctlst-gestured: missing runtime environment\n");
		return EXIT_FAILURE;
	}
	/*
	 * A second instance used to unlink the control sock and keep reading
	 * libinput, so one release could both commit and hide overview.
	 */
	if (acquire_singleton_lock(&state) < 0) {
		fprintf(stderr, "ctlst-gestured: already running\n");
		return EXIT_FAILURE;
	}

	state.command_fd = ipc_connect(state.sway_socket);
	state.control_fd = create_control_socket(&state);
	state.drawer_fd = socket(AF_UNIX,
	    SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (state.command_fd < 0 || state.control_fd < 0 ||
	    state.drawer_fd < 0) {
		perror("ctlst-gestured: connect");
		goto cleanup;
	}
	unlink(state.touch_state_path);
	unlink(state.click_block_path);
	if (!refresh_workspace(&state) || !refresh_output(&state)) {
		fprintf(stderr, "ctlst-gestured: cannot read Sway state\n");
		goto cleanup;
	}
	/* The workspace manager starts independently and pushes its first cache. */
	(void)refresh_workspace_cache(&state);
	if (init_touch_input(&state) < 0)
		input_retry_deadline = monotonic_us() + 2000000;
	snprintf(visible_path, sizeof(visible_path), "%s/ctlstdrawer.visible",
	    state.runtime_dir);
	state.drawer_visible = access(visible_path, F_OK) == 0;
	snprintf(visible_path, sizeof(visible_path), "%s/ctlstshade.visible",
	    state.runtime_dir);
	state.shade_visible = access(visible_path, F_OK) == 0;
	snprintf(visible_path, sizeof(visible_path), "%s/ctlstoverview.visible",
	    state.runtime_dir);
	state.overview_visible = access(visible_path, F_OK) == 0;
	set_controls_revealed(&state, false);
	init_haptic(&state);
	reset_gesture(&state);

	action.sa_handler = handle_signal;
	sigemptyset(&action.sa_mask);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);

	while (running) {
		char message[CONTROL_MESSAGE_SIZE];
		ssize_t size;
		nfds_t descriptor_count = 1;
		int timeout = -1;
		int ready;
		int64_t now = monotonic_us();

		if (state.input.fd < 0 && input_retry_deadline != 0 &&
		    input_retry_deadline <= now) {
			destroy_touch_input(&state.input);
			input_retry_deadline = init_touch_input(&state) < 0 ?
			    now + 2000000 : 0;
		}
		descriptors[0].fd = state.control_fd;
		descriptors[0].events = POLLIN;
		descriptors[0].revents = 0;
		if (state.input.fd >= 0) {
			descriptors[1].fd = state.input.fd;
			descriptors[1].events = POLLIN;
			descriptors[1].revents = 0;
			descriptor_count = 2;
		} else if (input_retry_deadline != 0) {
			int retry_timeout = (int)((input_retry_deadline - now + 999) /
			    1000);

			if (retry_timeout < 0)
				retry_timeout = 0;
			timeout = retry_timeout;
		}

		if (state.controls_revealed) {
			int64_t remaining = state.controls_reveal_deadline -
			    monotonic_us();

			if (remaining <= 0) {
				set_controls_revealed(&state, false);
				continue;
			}
			timeout = (int)((remaining + 999) / 1000);
		}
		if (state.click_block_deadline != 0) {
			int64_t remaining = state.click_block_deadline -
			    monotonic_us();
			int click_timeout;

			if (remaining <= 0) {
				unlink(state.click_block_path);
				state.click_block_deadline = 0;
				continue;
			}
			click_timeout = (int)((remaining + 999) / 1000);
			if (timeout < 0 || click_timeout < timeout)
				timeout = click_timeout;
		}
		if (state.tiled_hold_deadline != 0) {
			int64_t remaining = state.tiled_hold_deadline -
			    monotonic_us();
			int hold_timeout = remaining <= 0 ? 0 :
			    (int)((remaining + 999) / 1000);

			if (timeout < 0 || hold_timeout < timeout)
				timeout = hold_timeout;
		}
		if (state.right_click_deadline != 0) {
			int64_t remaining = state.right_click_deadline -
			    monotonic_us();
			int hold_timeout = remaining <= 0 ? 0 :
			    (int)((remaining + 999) / 1000);

			if (timeout < 0 || hold_timeout < timeout)
				timeout = hold_timeout;
		}
		if (state.wheel_reveal_deadline != 0) {
			int64_t remaining = state.wheel_reveal_deadline -
			    monotonic_us();
			int reveal_timeout = remaining <= 0 ? 0 :
			    (int)((remaining + 999) / 1000);

			if (timeout < 0 || reveal_timeout < timeout)
				timeout = reveal_timeout;
			if (state.gesture.mode == GESTURE_WORKSPACE_WHEEL_PENDING &&
			    !state.gesture.wheel_dock_revealed &&
			    (timeout < 0 ||
			    WORKSPACE_WHEEL_INFLATE_FRAME_MS < timeout))
				timeout = WORKSPACE_WHEEL_INFLATE_FRAME_MS;
		}
		ready = poll(descriptors, descriptor_count, timeout);

		if (ready < 0) {
			if (errno == EINTR)
				continue;
			perror("ctlst-gestured: poll");
			break;
		}
		/*
		 * Hold timers must fire even when control/touch fds keep waking
		 * poll (workspace-cache pushes, contact noise). Checking only on
		 * ready==0 starves the task-pill reveal.
		 */
		(void)arm_expired_tiled_hold(&state);
		(void)arm_expired_right_click(&state);
		(void)arm_expired_wheel_reveal(&state);
		if (state.controls_revealed &&
		    state.controls_reveal_deadline <= monotonic_us())
			set_controls_revealed(&state, false);
		if (state.click_block_deadline != 0 &&
		    state.click_block_deadline <= monotonic_us()) {
			unlink(state.click_block_path);
			state.click_block_deadline = 0;
		}
		if (ready == 0)
			continue;
		if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL))
			break;
		if (descriptors[0].revents & POLLIN) {
			size = recv(state.control_fd, message, sizeof(message) - 1, 0);
			if (size > 0) {
				message[size] = '\0';
				handle_control_message(&state, message, size);
			}
		}
		if (descriptor_count == 2 &&
		    descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			destroy_touch_input(&state.input);
			input_retry_deadline = monotonic_us() + 2000000;
		} else if (descriptor_count == 2 &&
		    descriptors[1].revents & POLLIN) {
			dispatch_touch_input(&state);
		}
	}
	exit_status = EXIT_SUCCESS;

cleanup:
	reset_gesture(&state);
	if (state.controls_revealed)
		set_controls_revealed(&state, false);
	if (state.command_fd >= 0)
		close(state.command_fd);
	if (state.control_fd >= 0)
		close(state.control_fd);
	if (state.lock_fd >= 0)
		close(state.lock_fd);
	if (state.drawer_fd >= 0)
		close(state.drawer_fd);
	if (state.haptic_fd >= 0) {
		if (state.haptic_effect_id >= 0)
			(void)ioctl(state.haptic_fd, EVIOCRMFF,
			    state.haptic_effect_id);
		close(state.haptic_fd);
	}
	destroy_touch_input(&state.input);
	if (state.control_path[0] != '\0')
		unlink(state.control_path);
	if (state.click_block_path[0] != '\0')
		unlink(state.click_block_path);
	return exit_status;
}
