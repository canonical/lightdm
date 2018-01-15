[![Build Status](https://travis-ci.org/CanonicalLtd/lightdm.svg?branch=lightdm-1-24)](https://travis-ci.org/CanonicalLtd/lightdm)

This version of LightDM is supported until April 2021.

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
