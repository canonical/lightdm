SUBDIRS = src

TESTS = \
	test-headless \
	test-autologin \
	test-login-gobject \
	test-login-gobject-wrong-password \
	test-login-gobject-invalid-user \
	test-login-gobject-logout \
	test-login-gobject-session-crash \
	test-login-gobject-guest \
	test-login-gobject-guest-no-config \
	test-login-gobject-guest-disabled \
	test-login-gobject-guest-no-setup-script \
	test-login-gobject-guest-fail-setup-script

# Disable no-config script, we can't specify display numbers currently so it
# conflicts with an existing running X server
#	test-no-config

EXTRA_DIST = \
	$(TESTS) \
	data/passwd.in \
	data/home/alice \    
	data/home/bob \
	data/home/carol \
	data/themes/login-alice/index.theme \
	data/themes/login-alice-wrong-password/index.theme \
	data/themes/login-invalid-user/index.theme \
	data/themes/test-theme/index.theme \
	data/xsessions/test-session-crash.desktop \
	data/xsessions/test-session.desktop \
	data/xsessions/test-session-logout.desktop \
	scripts/autologin.conf \
	scripts/autologin.script \
	scripts/headless.conf \
	scripts/headless.script \
	scripts/login-gobject.conf \
	scripts/login-gobject-invalid-user.conf \
	scripts/login-gobject-invalid-user.script \
	scripts/login-gobject-logout.conf \
	scripts/login-gobject-logout.script \
	scripts/login-gobject.script \
	scripts/login-gobject-session-crash.conf \
	scripts/login-gobject-session-crash.script \
	scripts/login-gobject-wrong-password.conf \
	scripts/login-gobject-wrong-password.script \
	scripts/login-gobject-guest.conf \
	scripts/login-gobject-guest.script \
	scripts/no-config.script
