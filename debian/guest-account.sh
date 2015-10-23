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

is_system_user ()
{
  UID_MIN=$(cat /etc/login.defs | grep UID_MIN | awk '{print $2}')
  SYS_UID_MIN=$(cat /etc/login.defs | grep SYS_UID_MIN | awk '{print $2}')
  SYS_UID_MAX=$(cat /etc/login.defs | grep SYS_UID_MAX | awk '{print $2}')

  SYS_UID_MIN=${SYS_UID_MIN:-101}
  SYS_UID_MAX=${SYS_UID_MAX:-$(( UID_MIN - 1 ))}

  [ ${1} -ge ${SYS_UID_MIN} ] && [ ${1} -le ${SYS_UID_MAX} ]
}

add_account ()
{
  temp_home=$(mktemp -td guest-XXXXXX)
  GUEST_HOME=$(echo ${temp_home} | tr '[:upper:]' '[:lower:]')
  GUEST_USER=${GUEST_HOME#/tmp/}
  [ ${GUEST_HOME} != ${temp_home} ] && mv ${temp_home} ${GUEST_HOME}

  # if ${GUEST_USER} already exists, it must be a locked system account with no existing
  # home directory
  if PWSTAT=$(passwd -S ${GUEST_USER}) 2>/dev/null; then
    if [ $(echo ${PWSTAT} | cut -f2 -d' ') != L ]; then
      echo "User account ${GUEST_USER} already exists and is not locked"
      exit 1
    fi

    PWENT=$(getent passwd ${GUEST_USER}) || {
      echo "getent passwd ${GUEST_USER} failed"
      exit 1
    }

    GUEST_UID=$(echo ${PWENT} | cut -f3 -d:)

    if ! is_system_user ${GUEST_UID}; then
      echo "Account ${GUEST_USER} is not a system user"
      exit 1
    fi

    GUEST_HOME=$(echo ${PWENT} | cut -f6 -d:)

    if [ ${GUEST_HOME} != / ] && [ ${GUEST_HOME#/tmp} = ${GUEST_HOME} ] && [ -d ${GUEST_HOME} ]; then
      echo "Home directory of ${GUEST_USER} already exists"
      exit 1
    fi
  else
    # does not exist, so create it
    useradd --system --home-dir ${GUEST_HOME} --comment $(gettext "Guest") --user-group --shell /bin/bash ${GUEST_USER} || {
      rm -rf ${GUEST_HOME}
      exit 1
    }
  fi

  dist_gs=/usr/share/lightdm/guest-session
  site_gs=/etc/guest-session

  # create temporary home directory
  mount -t tmpfs -o mode=700,uid=${GUEST_USER} none ${GUEST_HOME} || {
    rm -rf ${GUEST_HOME}
    exit 1
  }

  if [ -d ${site_gs}/skel ] && [ "$(ls -A ${site_gs}/skel)" ]; then
    # Only perform union-mounting if BindFS is available
    if [ -x /usr/bin/bindfs ]; then
      bindfs_mount=true

      # Try OverlayFS first
      if modinfo -n overlay >/dev/null 2>&1; then
        mkdir ${GUEST_HOME}/upper ${GUEST_HOME}/work
        chown ${GUEST_USER}:${GUEST_USER} ${GUEST_HOME}/upper ${GUEST_HOME}/work

        mount -t overlay -o lowerdir=${dist_gs}/skel:${site_gs}/skel,upperdir=${GUEST_HOME}/upper,workdir=${GUEST_HOME}/work overlay ${GUEST_HOME} || {
          umount ${GUEST_HOME}
          rm -rf ${GUEST_HOME}
          exit 1
        }
      # If OverlayFS is not available, try AuFS
      elif [ -x /sbin/mount.aufs ]; then
        mount -t aufs -o br=${GUEST_HOME}:${dist_gs}/skel:${site_gs}/skel none ${GUEST_HOME} || {
          umount ${GUEST_HOME}
          rm -rf ${GUEST_HOME}
          exit 1
        }
      # If none of them is available, fall back to copy over
      else
        cp -rT ${site_gs}/skel/ ${GUEST_HOME}
        cp -rT ${dist_gs}/skel/ ${GUEST_HOME}
        chown -R ${GUEST_USER}:${GUEST_USER} ${GUEST_HOME}
        bindfs_mount=false
      fi

      if ${bindfs_mount}; then
        # Wrap ${GUEST_HOME} in a BindFS mount, so that
        # ${GUEST_USER} will be seen as the owner of ${GUEST_HOME}'s contents.
        bindfs -u ${GUEST_USER} -g ${GUEST_USER} ${GUEST_HOME} ${GUEST_HOME} || {
          umount ${GUEST_HOME} # union mount
          umount ${GUEST_HOME} # tmpfs mount
          rm -rf ${GUEST_HOME}
          exit 1
        }
      fi
    # If BindFS is not available, just fall back to copy over
    else
      cp -rT ${site_gs}/skel/ ${GUEST_HOME}
      cp -rT ${dist_gs}/skel/ ${GUEST_HOME}
      chown -R ${GUEST_USER}:${GUEST_USER} ${GUEST_HOME}
    fi
  else
    cp -rT /etc/skel/ ${GUEST_HOME}
    cp -rT ${dist_gs}/skel/ ${GUEST_HOME}
    chown -R ${GUEST_USER}:${GUEST_USER} ${GUEST_HOME}
  fi

  # setup session
  su ${GUEST_USER} -c "env HOME=${GUEST_HOME} site_gs=${site_gs} ${dist_gs}/setup.sh"

  echo ${GUEST_USER}
}

remove_account ()
{
  GUEST_USER=${1}

  PWENT=$(getent passwd ${GUEST_USER}) || {
    echo "Error: invalid user ${GUEST_USER}"
    exit 1
  }

  GUEST_UID=$(echo ${PWENT} | cut -f3 -d:)

  if ! is_system_user ${GUEST_UID}; then
    echo "Error: user ${GUEST_USER} is not a system user."
    exit 1
  fi

  GUEST_HOME=$(echo ${PWENT} | cut -f6 -d:)

  # kill all remaining processes
  if [ -x /bin/loginctl ] || [ -x /usr/bin/loginctl ]; then
    loginctl kill-user ${GUEST_USER} >/dev/null || true
  else
    while ps h -u ${GUEST_USER} >/dev/null
    do
      killall -9 -u ${GUEST_USER} || true
      sleep 0.2;
    done
  fi

  if [ ${GUEST_HOME} = ${GUEST_HOME#/tmp/} ]; then
    echo "Warning: home directory ${GUEST_HOME} is not in /tmp/. It won't be removed."
  else
    umount ${GUEST_HOME} || umount -l ${GUEST_HOME} || true # BindFS mount
    umount ${GUEST_HOME} || umount -l ${GUEST_HOME} || true # union mount
    umount ${GUEST_HOME} || umount -l ${GUEST_HOME} || true # tmpfs mount
    rm -rf ${GUEST_HOME}
  fi

  # remove leftovers in /tmp
  find /tmp -mindepth 1 -maxdepth 1 -uid ${GUEST_UID} -print0 | xargs -0 rm -rf || true

  # remove possible {/run,}/media/guest-XXXXXX folder
  for media_dir in /run/media/${GUEST_USER} /media/${GUEST_USER}; do
    if [ -d ${media_dir} ]; then
      for dir in $(find ${media_dir} -mindepth 1 -maxdepth 1); do
        umount ${dir} || true
      done

      rmdir ${media_dir} || true
    fi
  done

  userdel ${GUEST_USER}
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
    echo "Usage: ${0} add"
    echo "       ${0} remove [account]"
    exit 1
esac
