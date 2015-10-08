#!/bin/sh -e
# (C) 2008 Canonical Ltd.
# Author: Martin Pitt <martin.pitt@ubuntu.com>
# License: GPL v2 or later
# modified by David D Lowe and Thomas Detoux
#
# Setup user and temporary home directory for guest session.
# If this succeeds, this script needs to print the username as the last line to
# stdout.

export TEXTDOMAINDIR=/usr/share/locale-langpack
export TEXTDOMAIN=lightdm

# set the system wide locale for gettext calls
if [ -f /etc/default/locale ]; then
  . /etc/default/locale
  LANGUAGE=
  export LANG LANGUAGE
fi

add_account ()
{
  HOME=`mktemp -td guest-XXXXXX`
  USER=`echo $HOME | sed 's/\(.*\)guest/guest/'`
  # Suppose that we have a "guest-template" user/group,
  # with home directory /home/guest-template.
  # If GROUP below could be set to "guest-template", we can
  # mount /home/guest-template on /etc/guest-session/skel
  # using bindfs with option "mirror-only=@guest-template", so
  # that all guest accounts would see files in /etc/guest-session/skel
  # as their own ones.
  GROUP="$USER"
  PRE_HOME="/tmp/.rw-${USER}"

  # if $USER already exists, it must be a locked system account with no existing
  # home directory
  if PWSTAT=`passwd -S "$USER"` 2>/dev/null; then
    if [ "`echo \"$PWSTAT\" | cut -f2 -d\ `" != "L" ]; then
      echo "User account $USER already exists and is not locked"
      exit 1
    fi
    PWENT=`getent passwd "$USER"` || {
      echo "getent passwd $USER failed"
      exit 1
    }
    GUEST_UID=`echo "$PWENT" | cut -f3 -d:`
    if [ "$GUEST_UID" -ge 500 ]; then
      echo "Account $USER is not a system user"
      exit 1
    fi
    HOME=`echo "$PWENT" | cut -f6 -d:`
    if [ "$HOME" != / ] && [ "${HOME#/tmp}" = "$HOME" ] && [ -d "$HOME" ]; then
      echo "Home directory of $USER already exists"
      exit 1
    fi
  else
    # does not exist, so create it
    adduser --system --no-create-home --home / --gecos $(gettext "Guest") --ingroup $GROUP --shell /bin/bash $USER || {
        umount "$HOME"
        rm -rf "$HOME"
        umount "$PRE_HOME"
        rm -rf "$PRE_HOME"
        exit 1
    }
  fi

  gs_skel=/etc/guest-session/skel/

  if [ -d "$gs_skel" ] && [ -n "`find $gs_skel -type f`" ]; then
    # create temporary home directory
    mkdir "$PRE_HOME"
    mount -t tmpfs -o mode=700 none "$PRE_HOME" || { rm -rf "$PRE_HOME" "$HOME"; exit 1; }
    chown $USER:$GROUP "$PRE_HOME"

    # Try OverlayFS first
    if modinfo -n overlay >/dev/null 2>&1; then
      mkdir ${PRE_HOME}/.upper ${PRE_HOME}/.work
      chown -R $USER:$GROUP $PRE_HOME/.*
      mount -t overlay -o lowerdir=$gs_skel,upperdir=${PRE_HOME}/.upper,workdir=${PRE_HOME}/.work overlay $HOME || {
        rm -rf "$HOME"
        umount "$PRE_HOME"
        rm -rf "$PRE_HOME"
        exit 1
      }
    # If OverlayFS is not available, try AuFS
    elif [ -x /sbin/mount.aufs ]; then
      mount -t aufs -o br=${PRE_HOME}:$gs_skel none $HOME || {
        rm -rf "$HOME"
        umount "$PRE_HOME"
        rm -rf "$PRE_HOME"
        exit 1
      }
    # If none of them is available, fall back to copy over
    else
      umount "$PRE_HOME"
      rm -rf "$PRE_HOME"
      cp -rT $gs_skel "$HOME"
      chown -R $USER:$GROUP "$HOME"
    fi
  else
    cp -rT /etc/skel/ "$HOME"
    chown -R $USER:$GROUP "$HOME"
  fi

  usermod -d "$HOME" "$USER"

  #
  # setup session
  #

  # disable some services that are unnecessary for the guest session
  mkdir --parents "$HOME"/.config/autostart
  cd /etc/xdg/autostart/
  services="jockey-kde.desktop jockey-gtk.desktop update-notifier.desktop user-dirs-update-gtk.desktop"
  for service in $services
  do
    if [ -e /etc/xdg/autostart/"$service" ] ; then
        cp "$service" "$HOME"/.config/autostart
        echo "X-GNOME-Autostart-enabled=false" >> "$HOME"/.config/autostart/"$service"
    fi
  done

  # disable Unity shortcut hint
  mkdir -p "$HOME"/.cache/unity
  touch "$HOME"/.cache/unity/first_run.stamp

  STARTUP="$HOME"/.config/autostart/startup-commands.desktop
  echo "[Desktop Entry]" > $STARTUP
  echo "Name=Startup commands" >> $STARTUP
  echo "Type=Application" >> $STARTUP
  echo "NoDisplay=true" >> $STARTUP
  echo "Exec=/usr/lib/lightdm/guest-session-auto.sh" >> $STARTUP

  echo "export DIALOG_SLEEP=4" >> "$HOME"/.profile

  mkdir -p "$HOME"/.kde/share/config
  echo "[Basic Settings]" >> "$HOME"/.kde/share/config/nepomukserverrc
  echo "Start Nepomuk=false" >> "$HOME"/.kde/share/config/nepomukserverrc

  echo "[Event]" >> "$HOME"/.kde/share/config/notificationhelper
  echo "hideHookNotifier=true" >> "$HOME"/.kde/share/config/notificationhelper
  echo "hideInstallNotifier=true" >> "$HOME"/.kde/share/config/notificationhelper
  echo "hideRestartNotifier=true" >> "$HOME"/.kde/share/config/notificationhelper

  # Load restricted session
  #dmrc='[Desktop]\nSession=guest-restricted'
  #/bin/echo -e "$dmrc" > "$HOME"/.dmrc

  # set possible local guest session preferences
  if [ -f /etc/guest-session/prefs.sh ]; then
      . /etc/guest-session/prefs.sh
  fi

  chown -R $USER:$GROUP "$HOME"

  echo $USER  
}

remove_account ()
{
  GUEST_USER=$1
  
  PWENT=`getent passwd "$GUEST_USER"` || {
    echo "Error: invalid user $GUEST_USER"
    exit 1
  }
  GUEST_UID=`echo "$PWENT" | cut -f3 -d:`
  GUEST_HOME=`echo "$PWENT" | cut -f6 -d:`
  GUEST_PRE_HOME=/tmp/.rw-$GUEST_USER

  if [ "$GUEST_UID" -ge 500 ]; then
    echo "Error: user $GUEST_USER is not a system user."
    exit 1
  fi

  if [ "${GUEST_HOME}" = "${GUEST_HOME#/tmp/}" ]; then
    echo "Error: home directory $GUEST_HOME is not in /tmp/."
    exit 1
  fi

  # kill all remaining processes
  while ps h -u "$GUEST_USER" >/dev/null; do 
    killall -9 -u "$GUEST_USER" || true
    sleep 0.2; 
  done

  umount "$GUEST_HOME" || umount -l "$GUEST_HOME" || true
  rm -rf "$GUEST_HOME"
  umount "$GUEST_PRE_HOME" || umount -l "$GUEST_PRE_HOME" || true
  rm -rf "$GUEST_PRE_HOME"

  # remove leftovers in /tmp
  find /tmp -mindepth 1 -maxdepth 1 -uid "$GUEST_UID" -print0 | xargs -0 rm -rf || true

  # remove possible /media/guest-XXXXXX folder
  if [ -d /media/"$GUEST_USER" ]; then
    for dir in $( find /media/"$GUEST_USER" -mindepth 1 -maxdepth 1 ); do
      umount "$dir" || true
    done
    rmdir /media/"$GUEST_USER" || true
  fi

  deluser --system "$GUEST_USER"
}

case "$1" in
  add)
    add_account
    ;;
  remove)
    if [ -z $2 ] ; then
      echo "Usage: $0 remove [account]"
      exit 1
    fi
    remove_account $2
    ;;
  *)
    echo "Usage: $0 add|remove"
    exit 1
esac
