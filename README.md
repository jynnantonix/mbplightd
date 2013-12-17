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


How to build
------------

    clang -O2 mbplightd.c -o mbplightd -lm


Usage
-----

* Verify that the sensor and backlight files exist on your system

```
ls -l /sys/class/backlight/nvidia_backlight/brightness
ls -l /sys/class/leds/smc::kbd_backlight/brightness
ls -l /sys/devices/platform/applesmc.768/light
```

  If any of these files don't exist, you will need to modify the
  appropriate path in the source code

* Choose a chroot directory (currently this is hardcoded to
  `/jail/mbplightd`) and make sure that only root has permissions for
  that directory

```
sudo mkdir -p /jail/mbplightd
sudo chmod 700 /jail/mbplightd
```

* Make sure the `nobody` user exists and verify that its uid is 99

```
cat /etc/passwd | grep nobody
```

* Run mbplightd

```
./mbplightd
```

TODO
----
- [ ] Add configurable options
- [ ] Allow users to manually set brightness