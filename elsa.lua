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
-- Last modified: Thu Sep 27 12:29:30 2012 mstenber
-- Edit time:     24 min
--

require 'mst'

-- iterate through the interfaces provided by the low-level elsai interface,
-- wrapping them in tables as we go, and leeching off all
function elsai_interfaces_iterator(state_c, k)
   local i
   if not k
   then
      i = elsa.elsai_if_get(state_c)
   else
      i = elsa.elsai_if_get_next(state_c, getmetatable(k).i)
   end
   if not i
   then
      return
   end
   local t = {}
   setmetatable(t, {i=i})
   t.name = elsa.elsai_if_get_name(c, i)
   t.index = elsa.elsai_if_get_index(c, i)
   t.priority = elsa.elsai_if_get_priority(c, i)
   print(i, mst.repr(t))
   return t
end

function elsai_interfaces(c)
   return elsai_interfaces_iterator, c, nil
end

-- similar iterator for lsas by type
function elsai_lsas_by_type_iterator(state, k)
   local c, type = unpack(state)
   if not k
   then
      l = elsa.elsai_get_lsa_by_type(c, type)
   else
      l = elsa.elsai_get_lsa_by_type_next(c, getmetatable(k).l)
   end
   if not l
   then
      return
   end
   local t = {}
   setmetatable(t, {l=l})
   t.type = elsa.elsai_lsa_get_type(l)
   assert(t.type == type, "invalid type for lsa")
   t.rid = elsa.elsai_lsa_get_rid(l)
   t.lsid = elsa.elsai_lsa_get_lsid(l)
   t.body = elsa.elsai_lsa_get_body(l)
   return t
end

function elsai_lsas_by_type(c, type)
   return elsai_lsas_by_type_iterator, {c, type}, nil
end

-- convenience methods to get array / table of interfaces

function elsai_lsa_array_by_type(c, type)
   local t = {}
   for l in elsai_lsas_by_type(c, type)
   do
      table.insert(t, l)
   end
   return t
end

function elsai_interface_table(c)
   local t = {}
   for i in elsai_interfaces(c)
   do
      t[i.name] = i
   end
   return t
end

function elsa_dispatch()
   local e = elsa.elsa_active_get()
   print('got active', e)
   local c = e.client
   print('got client', c)

   local it = elsai_interface_table(c)
   print('got interfaces', mst.repr(it))

   local l = elsai_lsa_array_by_type(c, elsa.LSA_T_LINK)
   print('got link array', mst.repr(l))
   
   local rid = elsa.elsai_get_rid(c)
   print('got rid', string.format('%x', rid))
end

print('hello from LUA')
