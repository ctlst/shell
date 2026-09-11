#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define DEVICE_NAME "CTLST VM Touchscreen"

static void
sleep_ms(long milliseconds)
{
	struct timespec delay = {
		.tv_sec = milliseconds / 1000,
		.tv_nsec = milliseconds % 1000 * 1000000,
	};

	while (nanosleep(&delay, &delay) < 0 && errno == EINTR)
		;
}

static void
emit_event(int fd, unsigned short type, unsigned short code, int value)
{
	struct input_event event = {
		.type = type,
		.code = code,
		.value = value,
	};

	if (write(fd, &event, sizeof(event)) != sizeof(event)) {
		perror("write input event");
		exit(EXIT_FAILURE);
	}
}

static void
frame(int fd)
{
	emit_event(fd, EV_SYN, SYN_REPORT, 0);
}

static void
set_axis(int fd, unsigned int code, int maximum)
{
	struct uinput_abs_setup setup = {
		.code = (unsigned short)code,
		.absinfo = {
			.minimum = 0,
			.maximum = maximum,
			.resolution = 10,
		},
	};

	if (ioctl(fd, UI_SET_ABSBIT, code) < 0 ||
	    ioctl(fd, UI_ABS_SETUP, &setup) < 0) {
		perror("configure axis");
		exit(EXIT_FAILURE);
	}
}

static int
create_device(int width, int height)
{
	struct uinput_setup setup = {
		.id = {
			.bustype = BUS_VIRTUAL,
			.vendor = 0x4354,
			.product = 0x4c53,
			.version = 1,
		},
	};
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0) {
		perror("open /dev/uinput");
		exit(EXIT_FAILURE);
	}
	strncpy(setup.name, DEVICE_NAME, sizeof(setup.name) - 1);
	if (ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0 ||
	    ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
	    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH) < 0 ||
	    ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0 ||
	    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT) < 0) {
		perror("configure uinput device");
		exit(EXIT_FAILURE);
	}
	set_axis(fd, ABS_X, width - 1);
	set_axis(fd, ABS_Y, height - 1);
	set_axis(fd, ABS_MT_SLOT, 9);
	set_axis(fd, ABS_MT_TRACKING_ID, 65535);
	set_axis(fd, ABS_MT_POSITION_X, width - 1);
	set_axis(fd, ABS_MT_POSITION_Y, height - 1);
	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
		perror("create uinput device");
		exit(EXIT_FAILURE);
	}
	return fd;
}

static void
touch_down(int fd, int x, int y)
{
	emit_event(fd, EV_ABS, ABS_MT_SLOT, 0);
	emit_event(fd, EV_ABS, ABS_MT_TRACKING_ID, 42);
	emit_event(fd, EV_ABS, ABS_MT_POSITION_X, x);
	emit_event(fd, EV_ABS, ABS_MT_POSITION_Y, y);
	emit_event(fd, EV_ABS, ABS_X, x);
	emit_event(fd, EV_ABS, ABS_Y, y);
	emit_event(fd, EV_KEY, BTN_TOUCH, 1);
	frame(fd);
}

static void
touch_move(int fd, int x, int y)
{
	emit_event(fd, EV_ABS, ABS_MT_POSITION_X, x);
	emit_event(fd, EV_ABS, ABS_MT_POSITION_Y, y);
	emit_event(fd, EV_ABS, ABS_X, x);
	emit_event(fd, EV_ABS, ABS_Y, y);
	frame(fd);
}

static void
touch_up(int fd)
{
	emit_event(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
	emit_event(fd, EV_KEY, BTN_TOUCH, 0);
	frame(fd);
}

static void
gesture(int fd, int duration, int start_x, int start_y, int end_x, int end_y)
{
	int steps = duration / 12;

	touch_down(fd, start_x, start_y);
	if (steps < 2)
		steps = 2;
	for (int step = 1; step <= steps; step++) {
		int x = start_x + (end_x - start_x) * step / steps;
		int y = start_y + (end_y - start_y) * step / steps;

		touch_move(fd, x, y);
		sleep_ms(duration / steps);
	}
	touch_up(fd);
	sleep_ms(500);
}

int
main(int argc, char **argv)
{
	int width;
	int height;
	int delay;
	int duration;
	int start_x;
	int start_y;
	int end_x;
	int end_y;
	int fd;
	int stream = argc == 5 && strcmp(argv[1], "--stdin") == 0;
	int result = EXIT_SUCCESS;

	if (!stream && argc != 9) {
		fprintf(stderr, "usage: %s WIDTH HEIGHT DELAY_MS DURATION_MS "
		    "START_X START_Y END_X END_Y\n"
		    "   or: %s --stdin WIDTH HEIGHT DELAY_MS\n", argv[0], argv[0]);
		return EXIT_FAILURE;
	}
	width = atoi(argv[stream ? 2 : 1]);
	height = atoi(argv[stream ? 3 : 2]);
	delay = atoi(argv[stream ? 4 : 3]);
	duration = stream ? 1 : atoi(argv[4]);
	if (width < 2 || height < 2 || delay < 0 || duration < 1)
		return EXIT_FAILURE;
	fd = create_device(width, height);
	if (!stream) {
		printf("%s\n", DEVICE_NAME);
		fflush(stdout);
	}
	sleep_ms(delay);
	if (stream) {
		char line[256], extra;
		puts("READY");
		fflush(stdout);
		while (fgets(line, sizeof(line), stdin) != NULL) {
			if (sscanf(line, "%d %d %d %d %d %c", &duration,
			    &start_x, &start_y, &end_x, &end_y, &extra) != 5 ||
			    duration < 1 || duration > 10000 || start_x < 0 ||
			    start_x >= width || end_x < 0 || end_x >= width ||
			    start_y < 0 || start_y >= height || end_y < 0 || end_y >= height) {
				fputs("invalid gesture command\n", stderr);
				result = EXIT_FAILURE;
				break;
			}
			gesture(fd, duration, start_x, start_y, end_x, end_y);
			puts("DONE");
			fflush(stdout);
		}
	} else {
		gesture(fd, duration, atoi(argv[5]), atoi(argv[6]),
		    atoi(argv[7]), atoi(argv[8]));
	}
	if (ioctl(fd, UI_DEV_DESTROY) < 0)
		perror("destroy uinput device");
	close(fd);
	return result;
}
