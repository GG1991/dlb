
# AX_HWLOC
# --------
AC_DEFUN([AX_HWLOC],
[
    AC_MSG_CHECKING([for HWLOC])
    AC_ARG_WITH([hwloc],
        AC_HELP_STRING([--with-hwloc@<:@=DIR@:>@], [add HWLOC support]),
        [], dnl Implicit: with_hwloc=$enableval
        [with_hwloc=check]
    )
    AC_MSG_RESULT([$with_hwloc])

    AS_IF([test "x$with_hwloc" != xno], [
        AS_IF([test -d "$with_hwloc"], [
            AS_IF([test -d "$with_hwloc/include"], [user_hwloc_includes="-I$with_hwloc/include"])
            AS_IF([test -d "$with_hwloc/lib"], [user_hwloc_libdir="-L$with_hwloc/lib"])
        ])

        ### HWLOC INCLUDES ###
        AX_VAR_PUSHVALUE([CPPFLAGS], [$user_hwloc_includes])
        AC_CHECK_HEADERS([hwloc.h], [
            HWLOC_CPPFLAGS="$user_hwloc_includes"
        ] , [
            AS_IF([test "x$with_hwloc" != xcheck], [AC_MSG_ERROR([Cannot find HWLOC headers])])
            with_hwloc=no
        ])
        AX_VAR_POPVALUE([CPPFLAGS])
    ])

    AS_IF([test "x$with_hwloc" != xno], [
        ### HWLOC LIBS ###
        AX_VAR_PUSHVALUE([LIBS], [""])
        AX_VAR_PUSHVALUE([LDFLAGS], [$user_hwloc_libdir])
        AC_SEARCH_LIBS([hwloc_topology_init], [hwloc], [
            HWLOC_LDFLAGS="$user_hwloc_libdir $LIBS"
        ], [
            AS_IF([test "x$with_hwloc" != xcheck], [AC_MSG_ERROR([Cannot find HWLOC libraries])])
            with_hwloc=no
        ])
        AX_VAR_POPVALUE([LDFLAGS])
        AX_VAR_POPVALUE([LIBS])
    ])

    AS_IF([test "x$with_hwloc" = xno], [
        AC_CHECK_PROG([lscpu], [lscpu], [yes])
        AS_IF([test "x$lscpu" != xyes], [
            AC_MSG_ERROR([neither lscpu nor hwloc were found, one of them is mandatory])
        ])
    ])

    AC_SUBST([HWLOC_CPPFLAGS])
    AC_SUBST([HWLOC_LDFLAGS])
    AM_CONDITIONAL([HAVE_HWLOC], [test "x$with_hwloc" != xno])
])
