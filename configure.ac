AC_INIT(fex, 0.8.18)
AC_CONFIG_SRCDIR(src/fexd.cpp)

AC_CONFIG_AUX_DIR(config)

AM_CONFIG_HEADER(src/config.h)
AC_LANG(C++)
AM_INIT_AUTOMAKE

FEX_CONF='$(sysconfdir)/fex.conf'
FEX_STATE='$(localstatedir)/fex'

AC_SUBST(FEX_CONF)
AC_SUBST(FEX_STATE)
AC_SUBST(USE_POLL)
AC_SUBST(USE_DNOTIFY)
AC_SUBST(USE_POLLING)
AC_SUBST(FEX_LINK)

AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug], [build a debug version])])

if test "$enable_debug" = "yes"; then
    CXXFLAGS="${CXXFLAGS} -O0 -g3";
else
    CXXFLAGS="-O2 -DNDEBUG ${CXXFLAGS}";
fi

AC_ARG_ENABLE([slink],
	[AS_HELP_STRING([--enable-slink], [link static libraries])])

if test "$enable_slink" = "yes"; then
    FEX_LINK="-static"
else
    FEX_LINK=""
fi

AC_ARG_WITH([dnotify],
	[AS_HELP_STRING([--with-dnotify], 
			[use dnotify mechanism if inotify is not available [default=yes]])])

if test "$with_dnotify" = "no"; then
 USE_POLL=""
 USE_DNOTIFY=""
else
 USE_POLL="-DUSE_POLL"
 USE_DNOTIFY="-DUSE_DNOTIFY"
fi


AC_ARG_WITH([polling],
	[AS_HELP_STRING([--with-polling], 
			[use polling mechanism if inotify and dnotify is not available [default=yes]])])

if test "$with_polling" = "no"; then
 USE_POLLING=""
else
 USE_POLLING="-DUSE_POLLING"
fi


AC_PROG_CC(gcc)
AC_LANG_CPLUSPLUS
AC_PROG_CXX
AC_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)

AC_CHECK_HEADERS([ext/malloc_allocator.h])
AC_CHECK_HEADERS([linux/inotify.h])

AC_CHECK_LIB([rsync],
  [rs_delta_file],
  [],
  [AC_MSG_ERROR([librsync must be installed])]) 


AC_CHECK_LIB([z],
  [compressBound],
  [],
  [AC_MSG_ERROR([libz must be installed])]) 


AC_CHECK_LIB([confuse],
  [cfg_init],
  [],
  [AC_MSG_ERROR([libconfuse must be installed])]) 


AC_CHECK_LIB([log4cpp],
  [main],
  [],
  [AC_MSG_ERROR([liblog4cpp must be installed])]) 

AC_HEADER_STDC
AC_HEADER_DIRENT
AC_HEADER_STAT

AC_OUTPUT(Makefile src/Makefile)
