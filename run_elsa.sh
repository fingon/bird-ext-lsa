#!/bin/bash -ue
#-*-sh-*-
#
# $Id: run_elsa.sh $
#
# Author: Markus Stenberg <fingon@iki.fi>
#
#  Copyright (c) 2012 Markus Stenberg
#       All rights reserved
#
# Created:       Thu Sep 27 12:51:34 2012 mstenber
# Last modified: Thu Sep 27 12:52:13 2012 mstenber
# Edit time:     1 min
#

# Propagate LUA_PATH so that required modules can be found more easily..
sudo LUA_PATH=$LUA_PATH ./bird -d -c bird6.conf.elsa
