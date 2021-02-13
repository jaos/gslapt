dnl Process this file with autoconf to produce a configure script.

AC_INIT([gslapt],[0.5.6],[woodwardj@jaos.org])
AC_CONFIG_SRCDIR([configure.ac])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src/])
IT_PROG_INTLTOOL([0.35.0])

AC_CANONICAL_HOST

AC_SEARCH_LIBS([strerror],[cposix])
AC_PROG_CC dnl now defaults to C2011

case "$host_cpu" in
i[[3456]]86) host_cpu=i386 ;CFLAGS="$CFLAGS -fpic";;
x86_64) host_cpu=x86_64 ;CFLAGS="$CFLAGS -fPIC";;
arm*) host_cpu=arm ;;
esac

AC_C_CONST
AC_C_VOLATILE
AC_FUNC_REALLOC
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_PROG_INSTALL
AC_CHECK_FUNCS([strchr])
AC_CHECK_FUNCS([strdup])
AC_CHECK_FUNCS([strndup])
AC_CHECK_FUNCS([strpbrk])
AC_CHECK_FUNCS([strstr])
AC_CHECK_HEADERS([libintl.h])
AC_CHECK_HEADER(curl/curl.h, [], [AC_MSG_ERROR(libcurl headers are missing)])
AC_CHECK_HEADER(slapt.h, [], [AC_MSG_ERROR(libslapt headers are missing)])
AC_CHECK_LIB(z,gzopen, [LIBS="$LIBS -lz"], [AC_MSG_ERROR(libz is missing)])
AC_CHECK_LIB(m,round, [LIBS="$LIBS -lm"], [AC_MSG_ERROR(libm is missing)])
AC_CHECK_LIB(curl,curl_global_init, [LIBS="$LIBS `curl-config --libs`"], [AC_MSG_ERROR(curl libs are missing)])
AC_SEARCH_LIBS([EVP_md5],[ssl crypto],[],[AC_MSG_ERROR(openssl libs are missing)])
AC_CHECK_LIB(gpgme,gpgme_new, [
    LIBS="`gpgme-config --libs` $LIBS"
    AC_CHECK_LIB(slapt,slapt_add_pkg_source_gpg_key,
      [AC_DEFINE(SLAPT_HAS_GPGME, 1, [libslapt has builtin gpgme support])],
      [AC_MSG_WARN(libslapt does not have gpgme support)])
], [AC_MSG_WARN(gpg support disabled)])
AC_CHECK_LIB(slapt,slapt_vector_t_init, [], [AC_MSG_ERROR(libslapt 0.11.1 or greater is required)])


glib_modules="glib-2.0 gthread-2.0 gmodule-2.0 gmodule-export-2.0"
PKG_CHECK_MODULES(GLIB, [$glib_modules])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GTK, [gtk+-3.0 >= 3.4.0])
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

AM_CONDITIONAL(POLKIT, false)
AC_ARG_WITH(
  [polkit],
  AS_HELP_STRING([--with-polkit], [use Polkit pkexec to start gslapt (default is auto)]),
  [use_polkit=$withval],
  [AC_CHECK_PROG([use_polkit], [pkexec], yes, [no])]
)
AC_ARG_WITH(
  [polkitactiondir],
  AS_HELP_STRING([--with-polkitactiondir], [Specify the location for the system polkit-1/actions path]),
  [polkitactiondir=$withval],
  [PKG_CHECK_VAR(polkitactiondir, polkit-gobject-1, actiondir)]
)
if test x"$use_polkit" = x"yes"; then
  AC_PATH_PROG([PKEXEC], [pkexec])
  AM_CONDITIONAL(POLKIT, true)
  AC_SUBST(polkitactiondir)
fi

GETTEXT_PACKAGE=gslapt
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])

AC_CONFIG_FILES([
Makefile
src/Makefile
po/Makefile.in
ui/Makefile
])
AC_OUTPUT

