# LightDM Display Manager
[![Test status](https://github.com/canonical/lightdm/actions/workflows/test.yaml/badge.svg)](https://github.com/canonical/lightdm/actions/workflows/test.yaml)
[![LightDM questions on AskUbuntu](https://img.shields.io/stackexchange/askubuntu/t/lightdm?color=brightgreen)](https://askubuntu.com/questions/tagged/lightdm)

LightDM is a lightweight, cross-desktop display manager. A display manager is a daemon that:
- Runs display servers (e.g. X) where necessary.
- Runs greeters to allow users to pick which user account and session type to use.
- Allows greeters to perform authentication using PAM.
- Runs session processes once authentication is complete.
- Provides remote graphical login options.

Key features of LightDM are:
- Cross-desktop - supports different desktop technologies (X, Wayland, Mir, etc)
- Lightweight - low memory usage and fast performance
- Supports remote login (incoming: XDMCP and VNC; outgoing: XDMCP and pluggable)
- Supports guest sessions
- Has a comprehensive test suite

The core LightDM project does not provide any greeter with it; you should install a greeter appropriate to your system. Popular greeter projects are:

 * [LightDM GTK+ Greeter](https://github.com/Xubuntu/lightdm-gtk-greeter) - a greeter that has moderate requirements (GTK+).
 * [LightDM KDE Greeter](https://invent.kde.org/plasma/lightdm-kde-greeter) - greeter by [KDE](https://kde.org) (Qt)
 * [LXQt Greeter](https://github.com/lxde/lxqt-lightdm-greeter) - greeter used in [LXQt](http://lxqt.org/) (Qt)
 * [Pantheon Greeter](https://github.com/elementary/greeter) - greeter used in [elementary OS](https://elementary.io/) (GTK+/Clutter).
 * [Unity Greeter](https://launchpad.net/unity-greeter) - greeter used in [Unity](https://launchpad.net/unity).
 * [WebKit2 Greeter](https://github.com/antergos/lightdm-webkit2-greeter) - greeter that can be themed using HTML/CSS/Javascript
 * Run with no greeter (automatic login only)
 * [Write your own...](https://www.freedesktop.org/wiki/Software/LightDM/Development/)

## Configuration

LightDM configuration is provided by the following files:

```
/usr/share/lightdm/lightdm.conf.d/*.conf
/etc/lightdm/lightdm.conf.d/*.conf
/etc/lightdm/lightdm.conf
```

System provided configuration should be stored in `/usr/share/lightdm/lightdm.conf.d/`. System administrators can override this configuration by adding files to `/etc/lightdm/lightdm.conf.d/` and `/etc/lightdm/lightdm.conf`. Files are read in the above order and combined together to make the LightDM configuration.

For example, if a sysadmin wanted to override the system configured default session (provided in `/usr/share/lightdm/lightdm.conf.d`) they should make a file `/etc/lightdm/lightdm.conf.d/50-myconfig.conf` with the following:

```
[Seat:*]
user-session=mysession
```

Configuration is in keyfile format. For most installations you will want to change the keys in the `[Seat:*]` section as this applies to all seats on the system (normally just one). A configuration file showing all the possible keys is provided in [`data/lightdm.conf`](https://github.com/canonical/lightdm/blob/main/data/lightdm.conf).

### Display Setup Script

LightDM can be configured to run an external shell script to setup displays.

If an display setup script is used, it must be:
 - Located under `/usr/share`
 - Owned by the user `lightdm` and group `lightdm`
 - It cannot print or log to any destination not accessible to LightDM

To test a configuration:
 - Install `xserver-xephyr`: `sudo apt install xserver-xephyr`
 - Run the test as user lightdm: `sudo -u lightdm lightdm --test-mode --debug`

Put the shell script reference in the LightDM configuration:

```
[Seat:*]
display-setup-script=/usr/share/example_display_setup_script.sh 
```

## Questions

[Stack Overflow](http://stackoverflow.com/search?q=lightdm) and [Ask Ubuntu](http://askubuntu.com/search?q=lightdm) are good sites for frequently asked questions.
