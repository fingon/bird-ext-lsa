#!/bin/bash -e
#-*-sh-*-
#
# $Id: run_elsa.sh $
#
# Author: Markus Stenberg <fingon@iki.fi>
#
# Copyright (c) 2012 cisco Systems, Inc.
#
# Created:       Thu Sep 27 12:51:34 2012 mstenber
# Last modified: Wed Oct 24 13:38:28 2012 mstenber
# Edit time:     4 min
#

# Propagate LUA_PATH so that required modules can be found more easily..

if [ $# = 1 -a "$1" = "-d" ]
then
    sudo ENABLE_MST_DEBUG=1 LUA_PATH=$LUA_PATH valgrind ./bird -d -c bird6.conf.elsa
else
    sudo LUA_PATH=$LUA_PATH ./bird -d -c bird6.conf.elsa
fi
