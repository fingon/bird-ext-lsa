#!/usr/bin/env lua
-- -*-lua-*-
--
-- $Id: elsa.lua $
--
-- Author: Markus Stenberg <fingon@iki.fi>
--
--  Copyright (c) 2012 Markus Stenberg
--       All rights reserved
--
-- Created:       Wed Sep 26 23:01:06 2012 mstenber
-- Last modified: Wed Sep 26 23:18:05 2012 mstenber
-- Edit time:     4 min
--

function elsa_dispatch()
   local e = elsa.elsa_active_get()
   print('got active', e)
   local c = e.client
   print('got client', c)
   local rid = elsa.elsai_get_rid(c)
   print('got rid', string.format('%x', rid))
end

print('hello from LUA')
