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
  HOME=$(mktemp -td guest-XXXXXX)
  USER=$(echo ${HOME} | sed 's/\(.*\)guest/guest/')

  # if ${USER} already exists, it must be a locked system account with no existing
  # home directory
  if PWSTAT=$(passwd -S ${USER}) 2>/dev/null; then
    if [ $(echo ${PWSTAT} | cut -f2 -d' ') != L ]; then
      echo "User account ${USER} already exists and is not locked"
      exit 1
    fi
    PWENT=$(getent passwd ${USER}) || {
      echo "getent passwd ${USER} failed"
      exit 1
    }
    GUEST_UID=$(echo ${PWENT} | cut -f3 -d:)
    if [ ${GUEST_UID} -ge 500 ]; then
      echo "Account ${USER} is not a system user"
      exit 1
    fi
    HOME=$(echo ${PWENT} | cut -f6 -d:)
    if [ ${HOME} != / ] && [ ${HOME#/tmp} = ${HOME} ] && [ -d ${HOME} ]; then
      echo "Home directory of ${USER} already exists"
      exit 1
    fi
  else
    # does not exist, so create it
    adduser --system --no-create-home --home / --gecos $(gettext "Guest") --group --shell /bin/bash ${USER} || {
      umount ${HOME}
      rm -rf ${HOME}
      exit 1
    }
  fi

  dist_gs=/usr/share/lightdm/guest-session
  site_gs=/etc/guest-session

  # create temporary home directory
  mount -t tmpfs -o mode=700,uid=${USER} none ${HOME} || {
    rm -rf ${HOME}
    exit 1
  }

  if [ -d ${site_gs}/skel ] && [ -n $(find ${site_gs}/skel -type f) ]; then
    # Only perform union-mounting if BindFS is available
    if [ -x /usr/bin/bindfs ]; then
      bindfs_mount=true

      # Try OverlayFS first
      if modinfo -n overlay >/dev/null 2>&1; then
        mkdir ${HOME}/upper ${HOME}/work
        chown ${USER}:${USER} ${HOME}/upper ${HOME}/work
        mount -t overlay -o lowerdir=${dist_gs}/skel:${site_gs}/skel,upperdir=${HOME}/upper,workdir=${HOME}/work overlay ${HOME} || {
          umount ${HOME}
          rm -rf ${HOME}
          exit 1
        }
      # If OverlayFS is not available, try AuFS
      elif [ -x /sbin/mount.aufs ]; then
        mount -t aufs -o br=${HOME}:${dist_gs}/skel:${site_gs}/skel none ${HOME} || {
          umount ${HOME}
          rm -rf ${HOME}
          exit 1
        }
      # If none of them is available, fall back to copy over
      else
        cp -rT ${site_gs}/skel/ ${HOME}
        cp -rT ${dist_gs}/skel/ ${HOME}
        chown -R ${USER}:${USER} ${HOME}
        bindfs_mount=false
      fi

      if ${bindfs_mount}; then
        # Wrap ${HOME} in a BindFS mount, so that
        # ${USER} will be seen as the owner of ${HOME}'s contents.
        bindfs -u ${USER} -g ${USER} ${HOME} ${HOME} || {
          umount ${HOME} # union mount
          umount ${HOME} # tmpfs mount
          rm -rf ${HOME}
          exit 1
        }
      fi
    # If BindFS is not available, just fall back to copy over
    else
      cp -rT ${site_gs}/skel/ ${HOME}
      cp -rT ${dist_gs}/skel/ ${HOME}
      chown -R ${USER}:${USER} ${HOME}
    fi
  else
    cp -rT /etc/skel/ ${HOME}
    cp -rT ${dist_gs}/skel/ ${HOME}
    chown -R ${USER}:${USER} ${HOME}
  fi

  usermod -d ${HOME} ${USER}

  # setup session
  su ${USER} -c "env HOME=${HOME} site_gs=${site_gs} ${dist_gs}/setup.sh"

  echo ${USER}
}

remove_account ()
{
  GUEST_USER=${1}
  
  PWENT=$(getent passwd ${GUEST_USER}) || {
    echo "Error: invalid user ${GUEST_USER}"
    exit 1
  }
  GUEST_UID=$(echo ${PWENT} | cut -f3 -d:)
  GUEST_HOME=$(echo ${PWENT} | cut -f6 -d:)

  if [ ${GUEST_UID} -ge 500 ]; then
    echo "Error: user ${GUEST_USER} is not a system user."
    exit 1
  fi

  if [ ${GUEST_HOME} = ${GUEST_HOME#/tmp/} ]; then
    echo "Error: home directory ${GUEST_HOME} is not in /tmp/."
    exit 1
  fi

  # kill all remaining processes
  while ps h -u ${GUEST_USER} >/dev/null; do 
    killall -9 -u ${GUEST_USER} || true
    sleep 0.2; 
  done

  umount ${GUEST_HOME} || umount -l ${GUEST_HOME} || true # BindFS mount
  umount ${GUEST_HOME} || umount -l ${GUEST_HOME} || true # union mount
  umount ${GUEST_HOME} || umount -l ${GUEST_HOME} || true # tmpfs mount
  rm -rf ${GUEST_HOME}

  # remove leftovers in /tmp
  find /tmp -mindepth 1 -maxdepth 1 -uid ${GUEST_UID} -print0 | xargs -0 rm -rf || true

  # remove possible /media/guest-XXXXXX folder
  if [ -d /media/${GUEST_USER} ]; then
    for dir in $(find /media/${GUEST_USER} -mindepth 1 -maxdepth 1); do
      umount ${dir} || true
    done

    rmdir /media/${GUEST_USER} || true
  fi

  deluser --system ${GUEST_USER}
}

case ${1} in
  add)
    add_account
    ;;
  remove)
    if [ -z ${2} ] ; then
      echo "Usage: ${0} remove [account]"
      exit 1
    fi
    remove_account ${2}
    ;;
  *)
    echo "Usage: ${0} add|remove"
    exit 1
esac
