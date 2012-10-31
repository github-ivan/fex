sbin_PROGRAMS = fexd 

nmstl_sources = nmstl_src/debug.cc  \
	nmstl_src/internal.cc nmstl_src/serial.cc

nmstl_headers = nmstl/atomic \
	nmstl/debug nmstl/io nmstl/net nmstl/ntime \
	nmstl/rpc nmstl/serial nmstl/terminal  \
	nmstl/tqueue  nmstl/callback nmstl/internal.h \
	nmstl/ioevent nmstl/netioevent nmstl/ptr  \
	nmstl/status nmstl/thread

#nmstl_src/thread.cc

fexd_SOURCES = fexd.cpp 		\
	configfile.cpp configfile.h 	\
	filelistener.cpp filelistener.h \
	connection.cpp connection.h	\
	server.cpp server.h		\
	client.cpp client.h		\
	rsync.cpp rsync.h		\
	modlog.cpp modlog.h             \
	dialog.cpp dialog.h             \
	watchpoint.cpp watchpoint.h     \
	logging.h serial.h		\
	imonitor.h imonitor.cpp		\
	$(nmstl_headers)		\
	$(nmstl_sources)


fexd_LDFLAGS = @FEX_LINK@

# set the include path found by configure
INCLUDES= $(all_includes) @USE_POLLING@ @USE_POLL@ @USE_DNOTIFY@ \
	-DFEX_CONF=\"@FEX_CONF@\" \
	-DFEX_STATE=\"@FEX_STATE@\" 



