#!/bin/sh

HOME=${HOME:-$(getent passwd $(whoami) | cut -f6 -d:)}
site_gs=${site_gs:-/etc/guest-session}

# disable some services that are unnecessary for the guest session
services="jockey-kde.desktop jockey-gtk.desktop update-notifier.desktop user-dirs-update-gtk.desktop"

for service in ${services}; do
  if [ -e /etc/xdg/autostart/${service} ]; then
    [ -f ${HOME}/.config/autostart/${service} ] || cp /etc/xdg/autostart/${service} ${HOME}/.config/autostart
    echo "X-GNOME-Autostart-enabled=false" >> ${HOME}/.config/autostart/${service}
  fi
done

# disable Unity shortcut hint
[ -d ${HOME}/.cache/unity ] || mkdir -p ${HOME}/.cache/unity
touch ${HOME}/.cache/unity/first_run.stamp

[ -d ${HOME}/.kde/share/config ] || mkdir -p ${HOME}/.kde/share/config
echo "[Basic Settings]" >> ${HOME}/.kde/share/config/nepomukserverrc
echo "Start Nepomuk=false" >> ${HOME}/.kde/share/config/nepomukserverrc

echo "[Event]" >> ${HOME}/.kde/share/config/notificationhelper
echo "hideHookNotifier=true" >> ${HOME}/.kde/share/config/notificationhelper
echo "hideInstallNotifier=true" >> ${HOME}/.kde/share/config/notificationhelper
echo "hideRestartNotifier=true" >> ${HOME}/.kde/share/config/notificationhelper

# Load restricted session
#dmrc='[Desktop]\nSession=guest-restricted'
#/bin/echo -e ${dmrc} > ${HOME}/.dmrc

# delay the launch of info dialog
echo "export DIALOG_SLEEP=4" >> ${HOME}/.profile

# set possible local guest session preferences
if [ -f ${site_gs}/prefs.sh ]; then
    . ${site_gs}/prefs.sh
fi
