// SPDX-License-Identifier: GPL-2.0-or-later
/* Run with the handset's bionic loader and stock gps.default.so. */
#define _GNU_SOURCE
#include "android-gps-abi.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static int output = -1;
static volatile sig_atomic_t stopping;
static pthread_mutex_t output_lock = PTHREAD_MUTEX_INITIALIZER;

static void ignore(void) { }
static void ignore_data(void *data) { (void)data; }
static void ignore_info(const void *data) { (void)data; }
static void ignore_capabilities(uint32_t capabilities) { (void)capabilities; }
static void stop_signal(int sig) { (void)sig; stopping = 1; }

static void nmea(int64_t timestamp, const char *sentence, int length)
{
	(void)timestamp;
	if (!sentence || length <= 0 || length > 1024 || sentence[0] != '$')
		return;
	pthread_mutex_lock(&output_lock);
	ssize_t written = write(output, sentence, length);
	if (written == length && sentence[length - 1] != '\n')
		(void)write(output, "\r\n", 2);
	pthread_mutex_unlock(&output_lock);
}

struct thread_start {
	void (*function)(void *);
	void *argument;
};

static void *thread_main(void *argument)
{
	struct thread_start start = *(struct thread_start *)argument;
	free(argument);
	start.function(start.argument);
	return NULL;
}

static pthread_t create_thread(const char *name, void (*function)(void *), void *arg)
{
	(void)name;
	pthread_t thread = 0;
	struct thread_start *start = malloc(sizeof(*start));
	if (!start)
		return 0;
	*start = (struct thread_start){function, arg};
	if (pthread_create(&thread, NULL, thread_main, start)) {
		free(start);
		return 0;
	}
	return thread;
}

static struct gps_callbacks callbacks = {
	.size = sizeof(callbacks),
	.location = ignore_data,
	.status = ignore_data,
	.satellites = ignore_data,
	.nmea = nmea,
	.capabilities = ignore_capabilities,
	.acquire_wakelock = ignore,
	.release_wakelock = ignore,
	.create_thread = create_thread,
	.request_time = ignore,
	.system_info = ignore_info,
	.gnss_satellites = ignore_data,
	.mediatek_extension1 = ignore,
	.mediatek_extension2 = ignore,
};

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: begonia-gps-nmea OUTPUT_PTY_LINK\n");
		return 2;
	}
	signal(SIGTERM, stop_signal);
	signal(SIGINT, stop_signal);
	signal(SIGPIPE, SIG_IGN);

	const char *hal_path = getenv("BEGONIA_GPS_HAL");
	void *library = dlopen(hal_path ? hal_path : "/vendor/lib64/hw/gps.default.so", RTLD_NOW);
	struct gps_hw_module *module = library ? dlsym(library, "HMI") : NULL;
	if (!module || !module->methods || !module->methods->open) {
		fprintf(stderr, "GPS HAL load failed: %s\n", dlerror());
		return 1;
	}
	struct gps_device *device = NULL;
	if (module->methods->open(module, "gps", (struct gps_hw_device **)&device) || !device) {
		fprintf(stderr, "GPS HAL open failed\n");
		return 1;
	}
	const struct gps_interface *gps = device->get_gps_interface(device);
	if (!gps || gps->size < sizeof(*gps) || gps->init(&callbacks)) {
		fprintf(stderr, "GPS HAL initialization failed\n");
		return 1;
	}
	output = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
	if (output < 0 || grantpt(output) || unlockpt(output)) {
		perror("GPS PTY");
		return 1;
	}
	const char *slave_name = ptsname(output);
	int slave = slave_name ? open(slave_name, O_RDWR | O_NOCTTY | O_CLOEXEC) : -1;
	struct termios settings;
	if (slave < 0 || tcgetattr(slave, &settings)) {
		perror("GPS PTY slave");
		return 1;
	}
	cfmakeraw(&settings);
	if (tcsetattr(slave, TCSANOW, &settings) || fchmod(slave, 0660)) {
		perror("GPS PTY configuration");
		return 1;
	}
	/* systemd's Group= gives the PTY to the configured kiosk group. */
	if (fchown(slave, getuid(), getgid())) {
		perror("GPS PTY ownership");
		return 1;
	}
	unlink(argv[1]);
	if (symlink(slave_name, argv[1])) {
		perror("GPS PTY link");
		return 1;
	}
	/* Standalone = 0, periodic = 0. No invented time or location is injected. */
	if (gps->set_position_mode(0, 0, 1000, 0, 0) || gps->start()) {
		fprintf(stderr, "GPS HAL start failed\n");
		unlink(argv[1]);
		return 1;
	}
	fprintf(stderr, "GPS started; NMEA at %s\n", argv[1]);
	while (!stopping) {
		/* Drain LIVI's u-blox identity polls; this receiver is MediaTek. */
		char discard[256];
		while (read(output, discard, sizeof(discard)) > 0) { }
		usleep(250000);
	}
	gps->stop();
	gps->cleanup();
	unlink(argv[1]);
	close(slave);
	close(output);
	return 0;
}
