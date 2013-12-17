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

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#define FAIL_IF(cond, fmt)                                                     \
  do {                                                                         \
    if (unlikely(cond)) {                                                      \
      fprintf(stderr, fmt ": %s\n", strerror(errno));                          \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

void signal_handler(int signal) {
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

void run_daemon(int brightness_fd, int backlight_fd, int sensor_fd) {
  char buf[16];
  int ambient_light;
  double new_val;
  const double denom = log(256);

  /* Create new session */
  FAIL_IF(setsid() < 0, "Unable to create new session");

  /* Jail ourselves into a chroot */
  FAIL_IF(chroot("/jail/mbplightd") != 0, "chroot failed");
  FAIL_IF(chdir("/") != 0, "chdir failed");

  /* Lose all unnecessary privileges */
  FAIL_IF(setresgid(99, 99, 99) != 0, "Unable to change group id");
  FAIL_IF(setresuid(99, 99, 99) != 0, "Unable to change user id");

  /* Set up signal handlers */
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGHUP, signal_handler);

  do {
    /* go to the beginning of each file */
    FAIL_IF(lseek(sensor_fd, 0, SEEK_SET) < 0, "Sensor seek error");
    FAIL_IF(lseek(brightness_fd, 0, SEEK_SET) < 0, "Brightness seek error");
    FAIL_IF(lseek(backlight_fd, 0, SEEK_SET) < 0, "Backlight seek error");

    /* get the backight value */
    FAIL_IF(read(sensor_fd, buf, sizeof(buf)) <= 0, "Sensor read error");

    if (unlikely(sscanf(buf, "(%d,", &ambient_light) <= 0)) {
      puts("Error parsing sensor data\n");
      exit(EXIT_FAILURE);
    }

    /* calculate the new value*/
    new_val = 128 + ((log(ambient_light + 1) / denom) * 895);

    /* set the brightness */
    if (unlikely(snprintf(buf, sizeof(buf), "%d", (int) new_val) ==
                 sizeof(buf))) {
      buf[sizeof(buf) - 1] = '\0';
      printf("Brightness value overflowed buffer: %s\n", buf);
      exit(EXIT_FAILURE);
    }
    FAIL_IF(write(brightness_fd, buf, strlen(buf)) < 0,
            "Brightness write failed");

    /* set the keyboard backlight */
    if (unlikely(snprintf(buf, sizeof(buf), "%d", (int)(new_val / 4)) ==
                 sizeof(buf))) {
      buf[sizeof(buf) - 1] = '\0';
      printf("Backight value overflowed buffer: %s\n", buf);
      exit(EXIT_FAILURE);
    }
    FAIL_IF(write(backlight_fd, buf, strlen(buf)) < 0,
            "Backlight write failed");

    sleep(2);
  } while (1);

  __builtin_unreachable();
}

int main() {
  int brightness_fd, sensor_fd, backlight_fd;
  pid_t pid;

  brightness_fd =
      open("/sys/class/backlight/nvidia_backlight/brightness", O_WRONLY);
  FAIL_IF(brightness_fd < 0, "Error opening brightness file");

  backlight_fd =
      open("/sys/class/leds/smc::kbd_backlight/brightness", O_WRONLY);
  FAIL_IF(backlight_fd < 0, "Error opening backlight file");

  sensor_fd = open("/sys/devices/platform/applesmc.768/light", O_RDONLY);
  FAIL_IF(sensor_fd < 0, "Error opening sensor file");

  pid = fork();
  FAIL_IF(pid < 0, "Could not fork child process");

  if (pid > 0) {
    /* successfully forked, terminate parent */
    exit(EXIT_SUCCESS);
  }

  /* child process, write to the brightness file */
  run_daemon(brightness_fd, backlight_fd, sensor_fd);

  __builtin_unreachable();
}
