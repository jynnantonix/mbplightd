/*
 *  mbplightd.c
 *
 *  Copyright (c) 2013 Chirantan Ekbote <chirantan.ekbote@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void signal_handler(int signal)
{
	switch (signal) {
	case SIGINT:
	case SIGTERM:
		printf("Received signal: %s\n", strsignal(signal));
		exit(EXIT_SUCCESS);
		break;
	case SIGHUP:
		puts("Received SIGHUP\n");
		break;
	default:
		printf("Unhandled signal: (%d) %s\n", signal, strsignal(signal));
		break;
	}
}

void run_daemon(int brightness_fd, int backlight_fd, int sensor_fd)
{
	FILE *brightness, *backlight, *sensor;
	int brightness_dup, backlight_dup, sensor_dup;
	int ambient_light, unused;
	double denom, new_val;

	if (setsid() < 0) {
		printf("Error calling setsid: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (chroot("/jail/mbplightd") != 0) {
		printf("Call to chroot failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (chdir("/") != 0) {
		printf("Call to chdir failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (setresgid(99, 99, 99) != 0) {
		printf("Call to setresgid failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (setresuid(99, 99, 99) != 0) {
		printf("Call to setresuid failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);

	denom = log(256);

	do {
		/* duplicate file descriptors */
		if ((brightness_dup = dup(brightness_fd)) < 0) {
			printf("Error duplicating brightness fd: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		if ((backlight_dup = dup(backlight_fd)) < 0) {
			printf("Error duplicating backlight fd: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		if ((sensor_dup = dup(sensor_fd)) < 0) {
			printf("Error duplicating sensor fd: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		/* open files */
		sensor = fdopen(sensor_dup, "r");
		if (sensor == NULL) {
			printf("Error opening sensor file: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		brightness = fdopen(brightness_dup, "w");
		if (brightness == NULL) {
			printf("Error opening brightness file: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		backlight = fdopen(backlight_dup, "w");
		if (backlight == NULL) {
			printf("Error opening backlight file: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		fseek(sensor, 0, SEEK_SET);
		if (fscanf(sensor, "(%d,%d)", &ambient_light, &unused) <= 0) {
			printf("Error reading sensor file. feof: %d, ferror: %d\n",
				feof(sensor), ferror(sensor));
			exit(EXIT_FAILURE);
		}

		new_val = 128 + ((log(ambient_light+1) / denom) * 895);
		fprintf(brightness, "%d", (int)new_val);
		fprintf(backlight, "%d", (int)(new_val / 4));

		fclose(sensor);
		fclose(backlight);
		fclose(brightness);

		sleep(2);
	} while (1);

	__builtin_unreachable();
}

int main()
{
	int brightness_fd, sensor_fd, backlight_fd;
	pid_t pid;

	brightness_fd = open("/sys/class/backlight/nvidia_backlight/brightness", O_WRONLY);
	if (brightness_fd < 0) {
		printf("Error opening brightness file: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	backlight_fd = open("/sys/class/leds/smc::kbd_backlight/brightness", O_WRONLY);
	if (backlight_fd < 0) {
		printf("Error opening backlight file: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	sensor_fd = open("/sys/devices/platform/applesmc.768/light", O_RDONLY);
	if (sensor_fd < 0) {
		printf("Error opening sensor file: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	pid = fork();
	if (pid < 0) {
		/* error during fork() */
		puts(strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (pid > 0) {
		/* successfully forked, terminate parent */
		exit(EXIT_SUCCESS);
	}

	/* child process, write to the brightness file */
	run_daemon(brightness_fd, backlight_fd, sensor_fd);

	__builtin_unreachable();
}
