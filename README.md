mbplightd
=========

mbplightd is a very simple daemon to control the screen brightness and
keyboard backlight on my Macbook Pro.

WARNING
-------

mbplightd is still very much under development, use at your own risk


How it works
------------

mbplightd reads in the ambient light sensor and then sets the screen
and keyboard backlight using a log scale.


Security
--------

mbplightd must be run as root so that it can access the system files
for the sensors and backlights.  However, it does not need access to
anything else and so it operates under the principle of least
privilege.  After it has obtained file descriptors for the files it
needs, it changes its user and group id to the `nobody` user (99 on my
system) and chroots itself into a directory where it has no
permissions (`/jail/mbplightd`).  This way, even if it gets compromised,
there is very little damage it can do.


Dependencies
------------
- [iniparser](http://ndevilla.free.fr/iniparser/)


How to build
------------

```
make
```


Usage
-----

* Verify that the sensor and backlight files exist on your system

```
ls -l /sys/class/backlight/nvidia_backlight/brightness
ls -l /sys/class/leds/smc::kbd_backlight/brightness
ls -l /sys/devices/platform/applesmc.768/light
```

  If any of these files don't exist, find the corresponding file for
  your system and update `mbplightd.conf`

* Choose a chroot directory (currently this defaults to
  `/jail/mbplightd`) and make sure that only root has permissions for
  that directory.  If you would like to use a different directory,
  update `mbplightd.conf` and replace the directory below with your
  chosen directory.

```
sudo mkdir -p /jail/mbplightd
sudo chmod 700 /jail/mbplightd
```

* Choose a uid and gid for the program.  This defaults to 99 (the
  `nobody` user)

* Run mbplightd

```
./mbplightd
```

TODO
----
- [x] Add configurable options
- [ ] Allow users to manually set brightness
