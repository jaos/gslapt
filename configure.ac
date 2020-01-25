dnl Process this file with autoconf to produce a configure script.

AC_INIT([gslapt], [0.5.6], [woodwardj@jaos.org])
AC_CONFIG_SRCDIR([configure.ac])
AM_INIT_AUTOMAKE([-Wall foreign])
AM_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR([src/])
IT_PROG_INTLTOOL([0.35.0])

AC_CANONICAL_HOST

AC_ISC_POSIX
AC_PROG_CC_C99

case "$host_cpu" in
i[[3456]]86) host_cpu=i386 ;CFLAGS="$CFLAGS -fpic";;
x86_64) host_cpu=x86_64 ;CFLAGS="$CFLAGS -fPIC";;
arm*) host_cpu=arm ;;
esac

AC_HEADER_STDC
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
gtk2_modules="gtk+-2.0 >= 2.24.0"
gtk3_modules="gtk+-3.0 >= 3.4.0"
AM_CONDITIONAL(GTK3, true)
AM_CONDITIONAL(GTK2, false)
AC_ARG_ENABLE(gtk3,
   [  --enable-gtk3                   Build with GTK3 support],
   [enable_gtk3=$enableval], [enable_gtk3=yes])
AC_ARG_ENABLE(gtk2,
   [  --enable-gtk2                   Build with GTK2 support],
   [enable_gtk2=$enableval], [enable_gtk2=no])
if test x"$enable_gtk2" = x"yes"; then
  PKG_CHECK_MODULES(GTK, [$gtk2_modules])
  AM_CONDITIONAL(GTK3, false)
  AM_CONDITIONAL(GTK2, true)
else
  PKG_CHECK_MODULES(GTK, [$gtk3_modules])
  AM_CONDITIONAL(GTK3, true)
  AM_CONDITIONAL(GTK2, false)
fi
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

AM_CONDITIONAL(POLKIT, false)
AC_ARG_WITH(
  [polkit],
  AC_HELP_STRING([--with-polkit], [use Polkit pkexec to start gslapt (default is auto)]),
  [use_polkit=$withval],
  [AC_CHECK_PROG([use_polkit], [pkexec], yes, [no])]
)
if test x"$use_polkit" = x"yes"; then
  AC_PATH_PROG([PKEXEC], [pkexec])
  AM_CONDITIONAL(POLKIT, true)
  AS_AC_EXPAND(DATADIR, $datadir)
fi

GETTEXT_PACKAGE=gslapt
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])
AM_GLIB_GNU_GETTEXT

AC_CONFIG_FILES([
Makefile
src/Makefile
po/Makefile.in
ui/Makefile
])
AC_OUTPUT

