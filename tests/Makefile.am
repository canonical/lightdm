SUBDIRS = src

TESTS = \
	test-headless \
	test-no-config \
	test-autologin \
	test-login-gobject \
	test-login-gobject-wrong-password \
	test-login-gobject-invalid-user \
	test-login-gobject-logout

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
	data/xsessions/test-session.desktop \
	scripts/autologin.conf \
	scripts/autologin.script \
	scripts/headless.conf \
	scripts/headless.script \
	scripts/login-gobject.conf \
	scripts/login-gobject-invalid-user.conf \
	scripts/login-gobject-invalid-user.script \
	scripts/login-gobject.script \
	scripts/login-gobject-wrong-password.conf \
	scripts/login-gobject-wrong-password.script \
	scripts/no-config.script
