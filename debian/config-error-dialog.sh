# Copyright (C) 2014 Canonical Ltd
# Author: Gunnar Hjalmarsson <gunnarhj@ubuntu.com>
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, version 3 of the License.
#
# See http://www.gnu.org/copyleft/gpl.html the full text of the license.

# This file may be sourced by the function source_with_error_check() in
# /usr/sbin/lightdm-session

export TEXTDOMAIN=lightdm
. /usr/bin/gettext.sh

PARA1=$(eval_gettext 'Error found when loading $CONFIG_FILE:')
PARA2=$(gettext 'As a result the session will not be configured correctly.
You should fix the problem as soon as feasible.')

TEXT="$PARA1

$(fold -s $ERR)

$PARA2"

if [ -x /usr/bin/kdialog ]; then
	TEXT_FILE=$(mktemp --tmpdir config-err-kdialog-XXXXXX)
	echo -n "$TEXT" > "$TEXT_FILE"
	kdialog --textbox "$TEXT_FILE" 500 300
	rm -f "$TEXT_FILE"
elif [ -x /usr/bin/zenity ]; then
	zenity --warning --no-wrap --no-markup --text="$TEXT"
fi
