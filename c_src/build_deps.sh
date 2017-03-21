#!/bin/bash

# Detect OS
if test -z "$TARGET_OS"; then
    TARGET_OS=`uname -s`
fi

# /bin/sh on Solaris is not a POSIX compatible shell, but /usr/bin/ksh is.

if [ `uname -s` = 'SunOS' -a "${POSIX_SHELL}" != "true" ]; then
    POSIX_SHELL="true"
    export POSIX_SHELL
    exec /usr/bin/ksh $0 $@
fi
unset POSIX_SHELL # clear it so if we invoke other scripts, they run as ksh as well

LEVELDB_VSN="tags/2.0.9"

SNAPPY_VSN="1.0.4"

set -e

if [ `basename $PWD` != "c_src" ]; then
    # originally "pushd c_src" of bash
    # but no need to use directory stack push here
    cd c_src
fi

BASEDIR="$PWD"
ROOTDIR=`dirname $BASEDIR`

# detecting gmake and if exists use it
# if not use make
# (code from github.com/tuncer/re2/c_src/build_deps.sh
which gmake 1>/dev/null 2>/dev/null && MAKE=gmake
MAKE=${MAKE:-make}

#=======================================================================
# Leveldb rules
#=======================================================================

#------------------------------------------------------------
# Get snappy.  Currently lives in eleveldb (even though leveldb
# depends on it!)
#------------------------------------------------------------

snappy_get_deps()
{
    if [ ! -f snappy-$SNAPPY_VSN.tar.gz ]; then
	git clone git://github.com/basho/eleveldb
	cp eleveldb/c_src/snappy-$SNAPPY_VSN.tar.gz .
	\rm -rf eleveldb
    fi
}

#------------------------------------------------------------
# Get leveldb
#------------------------------------------------------------

leveldb_get_google_deps()
{
    if [ ! -d leveldb_google ]; then
	git clone git://github.com/google/leveldb leveldb_google
    fi

    (cd leveldb_google; make)
}

leveldb_get_deps()
{
    if [ ! -d leveldb ]; then
	git clone git://github.com/basho/leveldb
	(cd leveldb && git checkout $LEVELDB_VSN)
	if [ "$BASHO_EE" = "1" ]; then
	    (cd leveldb && git submodule update --init)
	fi
    else
	(cd leveldb && dl=$(git diff $LEVELDB_VSN |wc -l) && [ $dl != 0 ] && >&2 echo "\033[0;31m WARN - local leveldb is out of sync with remote $LEVELDB_VSN\033[0m") || :
    fi
}

leveldb_clean()
{
    rm -rf system snappy-$SNAPPY_VSN
    if [ -d leveldb ]; then
	(cd leveldb && $MAKE clean)
    fi
    rm -f ../priv/leveldb_repair ../priv/sst_scan ../priv/sst_rewrite ../priv/perf_dump
}

leveldb_make()
{
    if [ ! -d snappy-$SNAPPY_VSN ]; then
	tar -xzf snappy-$SNAPPY_VSN.tar.gz
	(cd snappy-$SNAPPY_VSN && ./configure --prefix=$BASEDIR/system --libdir=$BASEDIR/system/lib --with-pic)
    fi
    
    if [ ! -f system/lib/libsnappy.a ]; then
	(cd snappy-$SNAPPY_VSN && $MAKE && $MAKE install)
    fi
    
    export CFLAGS="$CFLAGS -I $BASEDIR/system/include"
    export CXXFLAGS="$CXXFLAGS -I $BASEDIR/system/include"
    export LDFLAGS="$LDFLAGS -L$BASEDIR/system/lib"
    export LD_LIBRARY_PATH="$BASEDIR/system/lib:$LD_LIBRARY_PATH"
    export LEVELDB_VSN="$LEVELDB_VSN"
    
    leveldb_get_deps
    
    # hack issue where high level make is running -j 4
    #  and causes build errors in leveldb
    export MAKEFLAGS=
    
    (cd leveldb && $MAKE -j 3 all)
    (cd leveldb && $MAKE -j 3 tools)
    (cp leveldb/perf_dump leveldb/sst_rewrite leveldb/sst_scan leveldb/leveldb_repair ../priv)
}

#=======================================================================
# MQTT rules
#=======================================================================

mqtt_clean()
{
    files=`echo *.cc`
    if [ "$files" != '*.cc' ]; then
	\rm *.cc;
    fi

    files=`echo *.o`
    if [ "$files" != '*.o' ]; then
	\rm *.o;
    fi

    files=`echo *.h`
    if [ "$files" != '*.h' ]; then
	\rm *.h;
    fi

    files=`echo *.d`
    if [ "$files" != '*.d' ]; then
	\rm *.d;
    fi

    if [ -d srcs ]; then
	\rm -rf srcs
    fi

    if [ -d ../bin ]; then
	\rm -rf ../bin
    fi

    if [ -d ../ebin ]; then
	files=`echo ../ebin/*`
	if [ "$files" != '../ebin/*' ]; then
	    \rm ../ebin/*
	fi
    fi

    if [ -d ../priv ]; then
	files=`echo ../priv/*`
	if [ "$files" != '../priv/*' ]; then
	    \rm ../priv/*
	fi
    fi
}

#------------------------------------------------------------
# Make the standalone C++ library and executable
#------------------------------------------------------------

mqtt_cc_make()
{
    cp util/[A-Z]*.cc .
    cp util/*.h .

    cp mqtt/[A-Z]*.cc .
    cp mqtt/*.h .

    LIBCC=`echo [A-Z]*.cc`
    LIBOBJ=`echo [A-Z]*.o`

    MQTT_DEF_FLAGS="-DWITH_ERL=0 -DWITH_LEVELDB=$MQTT_USE_LEVELDB"
    MQTT_INC_FLAGS="-I leveldb/include -I $MQTT_INC_DIR"
    MQTT_LIBS="-L$MQTT_LIB_DIR -lmosquitto $ROOTDIR/c_src/leveldb/libleveldb.a -L$ROOTDIR/c_src/system/lib -lsnappy -lpthread"

    case "$TARGET_OS" in
	Darwin)
	    MQTT_LIB_FLAGS="-dynamiclib -install_name $ROOTDIR/priv/libcmqtt.so $MQTT_LIBS"
	    ;;
	*)
	    MQTT_LIB_FLAGS="-shared $MQTT_LIBS"
    esac

    # The following is needed or our versions of the leveldb calls
    # won't mangle to the same name

    MQTT_COMP_FLAGS=
    case "$TARGET_OS" in
	Darwin)
	    MQTT_COMP_FLAGS="-mmacosx-version-min=10.8"
    esac
    
    echo "Building MQTT lib sources"
    g++ $MQTT_COMP_FLAGS -c $LIBCC $MQTT_DEF_FLAGS $MQTT_INC_FLAGS
    echo "Linking MQTT lib sources"
    g++ $MQTT_COMP_FLAGS $MQTT_LIB_FLAGS $LIBOBJ -o $ROOTDIR/priv/libcmqtt.so 

    cp mqtt/tMqtt.cc .

    if [ ! -d ../bin ]; then
	mkdir ../bin
    fi
    
    echo "Building MQTT bin sources"
    g++ $MQTT_COMP_FLAGS -c tMqtt.cc $MQTT_INC_FLAGS
    echo "Linking MQTT bin sources"
    g++ $MQTT_COMP_FLAGS -o ../bin/tMqtt tMqtt.o $MQTT_DEF_FLAGS -L $ROOTDIR/priv -lcmqtt $MQTT_LIBS

    \rm *.cc *.h *.o
}

#------------------------------------------------------------
# Copy files needed for the erlang bundled lib into c_src
#------------------------------------------------------------

mqtt_erl_copy()
{
    if [ ! -d ./srcs ]; then
	mkdir ./srcs
    fi
    
    cp util/*.cc ./srcs
    cp util/*.h ./srcs

    cp mqtt/*.cc ./srcs
    cp mqtt/*.h ./srcs

    cp enif/*.cc ./srcs
    cp enif/*.h ./srcs
}

#=======================================================================
# Process rules here
#=======================================================================

case "$1" in

    #------------------------------------------------------------
    # If we are using leveldb, make sure deps exist
    #------------------------------------------------------------
    
    get-deps)
	if [ $MQTT_USE_LEVELDB == "1" ]; then
	    leveldb_get_deps;
	    snappy_get_deps;
	fi
	;;

    #------------------------------------------------------------
    # Remove any leveldb-related dirs
    #------------------------------------------------------------
    
    rm-deps)
	rm -rf leveldb system snappy-$SNAPPY_VSN*
	;;

    #------------------------------------------------------------
    # Clean up MQTT files and leveldb
    #------------------------------------------------------------
    
    clean)
	mqtt_clean;
	leveldb_clean;
	;;
    
    mqttonly)

	if [ ! -d ../priv ]; then
	    mkdir ../priv
	fi
	
	# Build leveldb if using leveldb
	
	if [ $MQTT_USE_LEVELDB == "1" ]; then
	    leveldb_make;
	fi

	# Copy relevant files, build C++ lib and executable
	
	mqtt_clean;
	mqtt_cc_make;
	;;
    
    #------------------------------------------------------------
    # Erlang-bundle build (aka 'make compile')
    #------------------------------------------------------------
    
    *)
	# Build leveldb if using leveldb
	
	if [ $MQTT_USE_LEVELDB == "1" ]; then
	    leveldb_make;
	fi

	# Copy relevant files, build C++ lib and executable
	
	mqtt_clean;
	mqtt_cc_make;

	# Set up to build erlang bundle
	
	mqtt_erl_copy;
	;;
esac
