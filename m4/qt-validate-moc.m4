dnl Qt6
AC_DEFUN([QT6_IF_QT6_MOC],
    AS_IF([$1 -v >/dev/null 2>/dev/null &&
        (test "`$1 -v 2<&1 | grep -o 'Qt [[[0-9]]]\+'`" = "Qt 6" ||
         test "`$1 -v 2<&1 | grep -o 'moc [[[0-9]]]\+'`" = "moc 6" ||
         test "`$1 -v 2<&1 | grep -o 'moc-qt[[[0-9]]]\+'`" = "moc-qt6")],
        [$2]))

dnl Set MOCCMD to $1 if it is the Qt 6 "moc".
AC_DEFUN([QT6_TRY_MOC],
    [QT6_IF_QT6_MOC([$1], [MOCCMD="$1"])])

dnl If a usable moc command is found set $1
AC_DEFUN([QT6_VALIDATE_MOC], [
    AC_MSG_CHECKING([the Qt 6 moc command])
    dnl Try to find a Qt 6 'moc'
    AS_IF([test "x$MOCCMD" = "x"],
        [for mocpath in "moc" "qtchooser -run-tool=moc -qt=6" "moc-qt6" \
                        "/usr/lib/qt6/moc" "/usr/lib/qt6/libexec/moc" \
                        "/usr/lib64/qt6/moc" "/usr/lib64/qt6/libexec/moc" \
                        "$prefix/lib/qt6/moc" "$prefix/lib/qt6/libexec/moc" \
                        "$libexecdir/moc" "$libexecdir/qt6/moc" \
                        "$libdir/qt6/moc" "$libdir/qt6/libexec/moc"
        do
            if test "x$MOCCMD" = "x" ; then
                QT6_TRY_MOC([$mocpath])
            fi
        done
        AS_IF([test "x$MOCCMD" = "x"],
            [AC_MSG_RESULT([not found]); AC_SUBST($1,"")],
            [AC_MSG_RESULT([$MOCCMD]); AC_SUBST($1,"$MOCCMD")])
        MOCCMD=""],
        [AC_MSG_ERROR(["MOCCMD should not be set"])])])