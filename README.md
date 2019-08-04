[![Build Status](https://travis-ci.org/Canonical/lightdm.svg?branch=master)](https://travis-ci.org/Canonical/lightdm)

LightDM is a cross-desktop display manager. A display manager is a daemon that:
- Runs display servers (e.g. X) where necessary.
- Runs greeters to allow users to pick which user account and session type to use.
- Allows greeters to perform authentication using PAM.
- Runs session processes once authentication is complete.
- Provides remote graphical login options.

Key features of LightDM are:
- Cross-desktop - supports different desktop technologies.
- Supports different display technologies (X, Mir, Wayland ...).
- Lightweight - low memory usage and fast performance.
- Guest sessions.
- Supports remote login (incoming - XDMCP, VNC, outgoing - XDMCP, pluggable).
- Comprehensive test suite.

Releases are synchronised with the [Ubuntu release schedule](https://wiki.ubuntu.com/Releases) and supported for the duration of each Ubuntu release. Each release is announced on the [mailing list](http://lists.freedesktop.org/mailman/listinfo/lightdm).

The core LightDM project does not provide any greeter with it and you should install a greeter appropriate to your system. Popular greeter projects are:

 * [LightDM GTK+ Greeter](https://launchpad.net/lightdm-gtk-greeter) - a greeter that has moderate requirements (GTK+).
 * [LightDM KDE](http://projects.kde.org/lightdm) - greeter used in [KDE](http://kde.org) (Qt)
 * [LXqt Greeter](https://github.com/lxde/lxqt-lightdm-greeter) - greeter used in [LXqt](http://lxqt.org/) (Qt)
 * [Pantheon Greeter](https://github.com/elementary/greeter) - greeter used in [elementary OS](https://elementary.io/) (GTK+/Clutter).
 * [Unity Greeter](https://launchpad.net/unity-greeter) - greeter used in [Unity](https://launchpad.net/unity).
 * [WebKit2 Greeter](https://github.com/antergos/lightdm-webkit2-greeter) - greeter that can be themed using HTML/CSS/Javascript
 * Run with no greeter (automatic login only)
 * [Write your own...](https://www.freedesktop.org/wiki/Software/LightDM/Development/)

# Configuration

LightDM configuration is provided by the following files:

```
/usr/share/lightdm/lightdm.conf.d/*.conf
/etc/lightdm/lightdm.conf.d/*.conf
/etc/lightdm/lightdm.conf
```

System provided configuration should be stored in `/usr/share/lightdm/lightdm.conf.d/`. System administrators can override this configuration by adding files to `/etc/lightdm/lightdm.conf.d/` and `/etc/lightdm/lightdm.conf`. Files are read in the above order and combined together to make the LightDM configuration.

For example, if a sysadmind wanted to override the system configured default session (provided in `/usr/share/lightdm/lightdm.conf.d`) they should make a file `/etc/lightdm/lightdm.conf.d/50-myconfig.conf` with the following:

```
[Seat:*]
user-session=mysession
```

Configuration is in keyfile format. For most installations you will want to change the keys in the `[Seat:*]` section as this applies to all seats on the system (normally just one). A configuration file showing all the possible keys is provided in [`data/lightdm.conf`](https://github.com/Canonical/lightdm/blob/master/data/lightdm.conf).

# Questions

Questions should be asked on the [mailing list](http://lists.freedesktop.org/mailman/listinfo/lightdm). All questions are welcome.

[Stack Overflow](http://stackoverflow.com/search?q=lightdm) and [Ask Ubuntu](http://askubuntu.com/search?q=lightdm) are good sites for frequently asked questions.
