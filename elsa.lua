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
-- Last modified: Thu Oct  4 12:37:33 2012 mstenber
-- Edit time:     50 min
--

require 'mst'
require 'ssloop'
require 'skv'
require 'elsa_pa'

elsaw = mst.create_class{class='elsaw', mandatory={'c'}}

function elsaw:init()
   -- something we need to do here?
end

function elsaw:iterate_lsa(f, criteria)
   mst.a(criteria, 'criteria mandatory')
   mst.a(criteria.type, 'criteria.type mandatory')
   for lsa in elsai_lsas_by_type(self.c, criteria.type)
   do
      f(lsa)
   end
end

function elsaw:iterate_if(rid, f)
   for i in elsai_interfaces(self.c)
   do
      f(i)
   end
end

-- iterate through the interfaces provided by the low-level elsai interface,
-- wrapping them in tables as we go, and leeching off all
function elsai_interfaces_iterator(c, k)
   local i
   if not k
   then
      i = elsa.elsai_if_get(c)
   else
      i = elsa.elsai_if_get_next(c, getmetatable(k).i)
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
   --mst.d(i, mst.repr(t))
   return t
end

function elsai_interfaces(c)
   return elsai_interfaces_iterator, c, nil
end

-- similar iterator for lsas by type
function elsai_lsas_by_type_iterator(state, k)
   local c, type = unpack(state)
   local l
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

function _debug_state()
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

local _elsa_pa = false

function get_elsa_pa()
   local e = elsa.elsa_active_get()
   mst.d('got active', e)
   
   local c = e.client
   mst.d('got client', c)
   
   if not _elsa_pa
   then
      local rid = elsa.elsai_get_rid(c)
      local ew = elsaw:new{c=c}
      local skv = skv.skv:new{long_lived=true}
      _elsa_pa = elsa_pa.elsa_pa:new{elsa=ew, skv=skv, rid=rid}
   else
      _elsa_pa.c = c
   end
   return _elsa_pa
end

function elsa_dispatch()
   -- run the event loop also once, in non-blocking mode (this isn't
   -- really pretty, but oh well) not like the local state
   -- communication was really that critical
   ssloop.loop():poll(0)

   -- XXX - should we check if we need to run the PA alg?
   -- If so, we should consult both SKV state, and LSA state
   local epa = get_elsa_pa()
   epa:run()
end

print('hello from LUA')
