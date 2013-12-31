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
#include <iniparser.h>
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

#define CONFIG_LOCATION "/etc/mbplightd.conf"

#define SENSOR_LOCATION "/sys/devices/platform/applesmc.768/light"
#define BRIGHTNESS_LOCATION "/sys/class/backlight/nvidia_backlight/brightness"
#define BACKLIGHT_LOCATION "/sys/class/leds/smc::kbd_backlight/brightness"
#define AC_STATUS_LOCATION "/sys/class/power_supply/ADP1/online"
#define POLL_INTERVAL 2

#define AC_MAX_BRIGHTNESS 1023
#define AC_MIN_BRIGHTNESS 127
#define BAT_MAX_BRIGHTNESS 895
#define BAT_MIN_BRIGHTNESS 63

#define AC_MAX_BACKLIGHT 255
#define AC_MIN_BACKLIGHT 31
#define BAT_MAX_BACKLIGHT 223
#define BAT_MIN_BACKLIGHT 15

#define CHROOT_DIR "/jail/mbplightd"
#define DEFAULT_GID 99
#define DEFAULT_UID 99

static struct config {
  char *sensor;      /* filename for the ambient light sensor */
  char *brightness;  /* filename for the brightness control */
  char *backlight;   /* filename for the backlight control */
  char *ac_status;   /* filename for the ac adaptor status */
  int poll_interval; /* number of seconds to wait between updates */

  int ac_max_brightness;  /* maximum brightness value when on AC power */
  int ac_min_brightness;  /* minimum brightness value when on AC power */
  int bat_max_brightness; /* maximum brightness value when on battery power */
  int bat_min_brightness; /* minimum brightness value when on battery power */

  int ac_max_backlight;  /* maximum backlight value when on AC power */
  int ac_min_backlight;  /* minimum backlight value when on AC power */
  int bat_max_backlight; /* maximum backlight value when on battery power */
  int bat_min_backlight; /* minimum backlight value when on battery power */

  char *chroot_dir; /* directory we should chroot into */
  int uid;          /* user id we should run under */
  int gid;          /* group id we should run under */
} config;

static int brightness_fd; /* file descriptor for screen brightness control */
static int backlight_fd;  /* file descriptor for keyboard backlight control */
static int sensor_fd;     /* file descriptor for ambient light sensor */
static int ac_status_fd;  /* file descriptor for the ac adaptor status */
static char fd_buf[16];   /* buffer used to read/write from file descriptors */

static void config_init(void) {
  dictionary *dict = iniparser_load(CONFIG_LOCATION);

  FAIL_IF(dict == NULL, "Error loading config file");

  config.sensor =
      strdup(iniparser_getstring(dict, "general:sensor", SENSOR_LOCATION));

  config.brightness = strdup(
      iniparser_getstring(dict, "general:brightness", BRIGHTNESS_LOCATION));

  config.backlight = strdup(
      iniparser_getstring(dict, "general:backlight", BACKLIGHT_LOCATION));

  config.ac_status = strdup(
      iniparser_getstring(dict, "general:ac_status", AC_STATUS_LOCATION));

  config.poll_interval =
      iniparser_getint(dict, "general:poll_interval", POLL_INTERVAL);

  config.ac_max_brightness =
      iniparser_getint(dict, "brightness:ac_max", AC_MAX_BRIGHTNESS);

  config.ac_min_brightness =
      iniparser_getint(dict, "brightness:ac_min", AC_MIN_BRIGHTNESS);

  config.bat_max_brightness =
      iniparser_getint(dict, "brightness:bat_max", BAT_MAX_BRIGHTNESS);

  config.bat_min_brightness =
      iniparser_getint(dict, "brightness:bat_min", BAT_MIN_BRIGHTNESS);

  config.ac_max_backlight =
      iniparser_getint(dict, "backlight:ac_max", AC_MAX_BACKLIGHT);

  config.ac_min_backlight =
      iniparser_getint(dict, "backlight:ac_min", AC_MIN_BACKLIGHT);

  config.bat_max_backlight =
      iniparser_getint(dict, "backlight:bat_max", BAT_MAX_BACKLIGHT);

  config.bat_min_backlight =
      iniparser_getint(dict, "backlight:bat_min", BAT_MIN_BACKLIGHT);

  config.chroot_dir =
      strdup(iniparser_getstring(dict, "security:chroot_dir", CHROOT_DIR));

  config.gid = iniparser_getint(dict, "security:gid", DEFAULT_GID);

  config.uid = iniparser_getint(dict, "security:uid", DEFAULT_UID);

  iniparser_freedict(dict);
}

static void set_screen_brightness(int sensor, int low, int high) {
  const double denom = log(256); /* log of the max sensor value */
  int value = (int)(low + (log(sensor + 1.0) / denom) * (high - low));

  FAIL_IF(lseek(brightness_fd, 0, SEEK_SET) < 0, "Brightness seek error");
  /* set the brightness */
  if (unlikely(snprintf(fd_buf, sizeof(fd_buf), "%d", value) ==
               sizeof(fd_buf))) {
    fd_buf[sizeof(fd_buf) - 1] = '\0';
    fprintf(stderr, "Brightness value overflowed buffer: %s\n", fd_buf);
    exit(EXIT_FAILURE);
  }
  FAIL_IF(write(brightness_fd, fd_buf, strlen(fd_buf)) < 0,
          "Brightness write failed");
}

static void set_keyboard_backlight(int sensor, int low, int high) {
  const double denom = log(128);
  int value;

  if (sensor > 128) {
    value = 0;
  } else {
    sensor = abs(sensor - 4);
    value = (int)(low + (log(sensor + 1.0) / denom) * (high - low));
  }

  FAIL_IF(lseek(backlight_fd, 0, SEEK_SET) < 0, "Backlight seek error");
  /* set the keyboard backlight */
  if (unlikely(snprintf(fd_buf, sizeof(fd_buf), "%d", value) ==
               sizeof(fd_buf))) {
    fd_buf[sizeof(fd_buf) - 1] = '\0';
    fprintf(stderr, "Backlight value overflowed buffer: %s\n", fd_buf);
    exit(EXIT_FAILURE);
  }
  FAIL_IF(write(backlight_fd, fd_buf, strlen(fd_buf)) < 0,
          "Backlight write failed");
}

static int read_ambient_light_sensor(void) {
  int val;

  FAIL_IF(lseek(sensor_fd, 0, SEEK_SET) < 0, "Sensor seek error");

  /* read the sensor */
  FAIL_IF(read(sensor_fd, fd_buf, sizeof(fd_buf)) <= 0, "Sensor read error");
  if (unlikely(sscanf(fd_buf, "(%d,", &val) <= 0)) {
    fputs("Error parsing sensor data\n", stderr);
    exit(EXIT_FAILURE);
  }

  return val;
}

static int get_ac_status(void) {
  int val;

  FAIL_IF(lseek(ac_status_fd, 0, SEEK_SET) < 0, "Adaptor status seek error");

  /* read the ac adaptor status */
  FAIL_IF(read(ac_status_fd, fd_buf, sizeof(fd_buf)) <= 0,
          "Adaptor status read error");
  if (unlikely(sscanf(fd_buf, "%d,", &val) <= 0)) {
    fputs("Error parsing sensor data\n", stderr);
    exit(EXIT_FAILURE);
  }

  return val;
}

static void run_daemon(void) {
  int ambient_light;

  /* Create new session */
  FAIL_IF(setsid() < 0, "Unable to create new session");

  /* Jail ourselves into a chroot */
  FAIL_IF(chroot(config.chroot_dir) != 0, "chroot failed");
  FAIL_IF(chdir("/") != 0, "chdir failed");

  /* Lose all unnecessary privileges */
  FAIL_IF(setresgid(config.gid, config.gid, config.gid) != 0,
          "Unable to change group id");
  FAIL_IF(setresuid(config.uid, config.uid, config.uid) != 0,
          "Unable to change user id");

  do {
    ambient_light = read_ambient_light_sensor();

    if (get_ac_status() == 1) {
      set_screen_brightness(ambient_light, config.ac_min_brightness,
                            config.ac_max_brightness);
      set_keyboard_backlight(ambient_light, config.ac_min_backlight,
                             config.ac_max_backlight);
    } else {
      set_screen_brightness(ambient_light, config.bat_min_brightness,
                            config.bat_max_brightness);
      set_keyboard_backlight(ambient_light, config.bat_min_backlight,
                             config.bat_max_backlight);
    }

    sleep(config.poll_interval);
  } while (1);

  __builtin_unreachable();
}

int main() {
  pid_t pid;
  config_init();

  brightness_fd = open(config.brightness, O_WRONLY);
  FAIL_IF(brightness_fd < 0, "Error opening brightness file");

  backlight_fd = open(config.backlight, O_WRONLY);
  FAIL_IF(backlight_fd < 0, "Error opening backlight file");

  sensor_fd = open(config.sensor, O_RDONLY);
  FAIL_IF(sensor_fd < 0, "Error opening sensor file");

  ac_status_fd = open(config.ac_status, O_RDONLY);
  FAIL_IF(ac_status_fd < 0, "Error opening AC adaptor status file");

  pid = fork();
  FAIL_IF(pid < 0, "Could not fork child process");

  if (pid > 0) {
    /* successfully forked, terminate parent */
    exit(EXIT_SUCCESS);
  }

  /* child process, write to the brightness file */
  run_daemon();

  __builtin_unreachable();
}
