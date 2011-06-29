SUBDIRS = src

TESTS = \
	test-headless \
	test-autologin \
	test-autologin-logout \
	test-login-gobject \
	test-login-gobject-wrong-password \
	test-login-gobject-invalid-user \
	test-login-gobject-logout \
	test-login-gobject-xserver-crash \
	test-login-gobject-session-crash \
	test-login-gobject-guest \
	test-login-gobject-guest-no-config \
	test-login-gobject-guest-disabled \
	test-login-gobject-guest-no-setup-script \
	test-login-gobject-guest-fail-setup-script

# In progress
#	test-autologin-xserver-crash
#	test-xserver-fail-start
#	test-greeter-xserver-crash

# Disable no-config script, we can't specify display numbers currently so it
# conflicts with an existing running X server
#	test-no-config

EXTRA_DIST = \
	$(TESTS) \
	data/themes/test-theme/index.theme \
	data/xsessions/test-session.desktop \
	scripts/autologin.conf \
	scripts/autologin.script \
	scripts/autologin-logout.conf \
	scripts/autologin-logout.script \
	scripts/headless.conf \
	scripts/headless.script \
	scripts/login-gobject.conf \
	scripts/login-gobject.script \
	scripts/login-gobject-guest.conf \
	scripts/login-gobject-guest.script \
	scripts/login-gobject-guest-disabled.conf \
	scripts/login-gobject-guest-disabled.script \
	scripts/login-gobject-guest-fail-setup-script.conf \
	scripts/login-gobject-guest-fail-setup-script.script \
	scripts/login-gobject-guest-no-config.conf \
	scripts/login-gobject-guest-no-config.script \
	scripts/login-gobject-guest-no-setup-script.conf \
	scripts/login-gobject-guest-no-setup-script.script \
	scripts/login-gobject-invalid-user.conf \
	scripts/login-gobject-invalid-user.script \
	scripts/login-gobject-logout.conf \
	scripts/login-gobject-logout.script \
	scripts/login-gobject-session-crash.conf \
	scripts/login-gobject-session-crash.script \
	scripts/login-gobject-wrong-password.conf \
	scripts/login-gobject-wrong-password.script \
	scripts/login-gobject-xserver-crash.conf \
	scripts/login-gobject-xserver-crash.script \
	scripts/no-config.script
