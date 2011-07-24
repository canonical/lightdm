SUBDIRS = src

TESTS = \
	test-no-config \
	test-headless \
	test-autologin \
	test-autologin-guest \
	test-autologin-guest-fail-setup-script \
	test-autologin-invalid-user \
	test-autologin-logout \
	test-login-gobject \
	test-login-gobject-manual \
	test-login-gobject-no-password \
	test-login-gobject-wrong-password \
	test-login-gobject-invalid-user \
	test-login-gobject-logout \
	test-login-gobject-xserver-crash \
	test-login-gobject-session-crash \
	test-login-gobject-guest \
	test-login-gobject-guest-disabled \
	test-login-gobject-guest-no-setup-script \
	test-login-gobject-guest-fail-setup-script \
	test-plymouth-active-vt \
	test-plymouth-inactive-vt \
	test-plymouth-xserver-fail-start \
	test-switch-to-greeter \
	test-switch-to-guest \
	test-switch-to-user \
	test-xdmcp-login

#	test-autologin-invalid-session
#	test-autologin-session-crash
#	test-autologin-session-exit-error

if COMPILE_LIBLIGHTDM_QT
TESTS += \
	test-login-qt \
	test-login-qt-manual \
	test-login-qt-no-password \
	test-login-qt-wrong-password \
	test-login-qt-invalid-user \
	test-login-qt-logout \
	test-login-qt-xserver-crash \
	test-login-qt-session-crash \
	test-login-qt-guest \
	test-login-qt-guest-disabled \
	test-login-qt-guest-no-setup-script \
	test-login-qt-guest-fail-setup-script
endif

# In progress
#	test-autologin-xserver-crash
#	test-xserver-fail-start
#	test-greeter-xserver-crash

EXTRA_DIST = \
	$(TESTS) \
	data/xgreeters/test-gobject-greeter.desktop \
	data/xgreeters/test-qt-greeter.desktop \
	data/xsessions/test-session.desktop \
	scripts/autologin.conf \
	scripts/autologin.script \
	scripts/autologin-guest.conf \
	scripts/autologin-guest.script \
	scripts/autologin-guest-fail-setup-script.conf \
	scripts/autologin-guest-fail-setup-script.script \
	scripts/autologin-invalid-user.conf \
	scripts/autologin-invalid-user.script \
	scripts/autologin-logout.conf \
	scripts/autologin-logout.script \
	scripts/headless.conf \
	scripts/headless.script \
	scripts/login.conf \
	scripts/login.script \
	scripts/login-guest.conf \
	scripts/login-guest.script \
	scripts/login-guest-disabled.conf \
	scripts/login-guest-disabled.script \
	scripts/login-guest-fail-setup-script.conf \
	scripts/login-guest-fail-setup-script.script \
	scripts/login-guest-no-setup-script.conf \
	scripts/login-guest-no-setup-script.script \
	scripts/login-invalid-user.conf \
	scripts/login-invalid-user.script \
	scripts/login-logout.conf \
	scripts/login-logout.script \
	scripts/login-manual.conf \
	scripts/login-manual.script \
	scripts/login-no-password.conf \
	scripts/login-no-password.script \
	scripts/login-session-crash.conf \
	scripts/login-session-crash.script \
	scripts/login-wrong-password.conf \
	scripts/login-wrong-password.script \
	scripts/login-xserver-crash.conf \
	scripts/login-xserver-crash.script \
	scripts/no-config.script \
	scripts/plymouth-active-vt.conf \
	scripts/plymouth-active-vt.script \
	scripts/plymouth-inactive-vt.conf \
	scripts/plymouth-inactive-vt.script \
	scripts/plymouth-xserver-fail-start.conf \
	scripts/plymouth-xserver-fail-start.script \
	scripts/switch-to-greeter.conf \
	scripts/switch-to-greeter.script \
	scripts/switch-to-guest.conf \
	scripts/switch-to-guest.script \
	scripts/switch-to-user.conf \
	scripts/switch-to-user.script \
	scripts/xdmcp-login.conf \
	scripts/xdmcp-login.script
