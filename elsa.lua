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
-- Last modified: Mon Oct  8 13:56:41 2012 mstenber
-- Edit time:     87 min
--

require 'mst'
require 'ssloop'
require 'skv'
require 'elsa_pa'
require 'linux_if'

elsaw = mst.create_class{class='elsaw', mandatory={'c'}}

function elsaw:init()
   -- something we need to do here?
   self.last = ''
   self.sn = 0

   -- calculate the hardware fingerprint
   local if_table = linux_if.if_table:new{shell=mst.execute_to_string}
   local m = if_table:read_ip_ipv6()
   local t = mst.array:new()
   
   for ifname, ifo in pairs(m)
   do
      local hwa = ifo:get_hwaddr()
      if hwa
      then
         t:insert(hwa)
      end
   end

   -- must have at least one hw address
   mst.a(#t > 0)
   
   -- in-place sort 
   t:sort()
   self.hwf = t:join(' ')

   mst.d('got hwf', self.hwf)

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

function elsaw:originate_lsa(d)
   self:a(d.type == elsa_pa.AC_TYPE, 'support for other types missing')
   if self.body == d.body
   then
      mst.d('skipped duplicate LSA')
      return
   end
   self.sn = self.sn + 1
   elsac.elsai_lsa_originate(self.c, d.type, 0, self.sn, self.last)
end

function elsaw:change_rid()
   mst.d('changing rid')
   elsac.change_rid(self.c)
end

function elsaw:get_hwf()
   return self.hwf
end

-- iterate through the interfaces provided by the low-level elsai interface,
-- wrapping them in tables as we go, and leeching off all
function elsai_interfaces_iterator(c, k)
   local i
   if not k
   then
      i = elsac.elsai_if_get(c)
   else
      i = elsac.elsai_if_get_next(c, getmetatable(k).i)
   end
   if not i
   then
      return
   end
   local t = {}
   setmetatable(t, {i=i})
   t.name = elsac.elsai_if_get_name(c, i)
   t.index = elsac.elsai_if_get_index(c, i)
   t.priority = elsac.elsai_if_get_priority(c, i)
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
      l = elsac.elsai_get_lsa_by_type(c, type)
   else
      l = elsac.elsai_get_lsa_by_type_next(c, getmetatable(k).l)
   end
   if not l
   then
      return
   end
   local t = {}
   setmetatable(t, {l=l})
   t.type = elsac.elsai_lsa_get_type(l)
   assert(t.type == type, "invalid type for lsa")
   t.rid = elsac.elsai_lsa_get_rid(l)
   t.lsid = elsac.elsai_lsa_get_lsid(l)
   t.body = elsac.elsai_lsa_get_body(l)
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
   local e = elsac.elsa_active_get()
   print('got active', e)

   local c = e.client
   print('got client', c)

   local it = elsai_interface_table(c)
   print('got interfaces', mst.repr(it))

   local l = elsai_lsa_array_by_type(c, elsac.LSA_T_LINK)
   print('got link array', mst.repr(l))
   
   local rid = elsac.elsai_get_rid(c)
   print('got rid', string.format('%x', rid))
end

local _elsa_pa = false

function get_elsa_pa()
   local e = elsac.elsa_active_get()
   mst.d('got active', e)
   
   local c = e.client
   mst.d('got client', c)
   
   local rid = elsac.elsai_get_rid(c)
   mst.d('got rid', string.format('%x', rid))

   if not _elsa_pa
   then
      local ew = elsaw:new{c=c}
      local skv = skv.skv:new{long_lived=true}
      _elsa_pa = elsa_pa.elsa_pa:new{elsa=ew, skv=skv, rid=rid}
   else
      _elsa_pa.c = c
      _elsa_pa.rid = rid
   end
   return _elsa_pa
end

function elsa_dispatch()
   mst.d_xpcall(function ()

                   -- run the event loop also once, in non-blocking
                   -- mode (this isn't really pretty, but oh well) not
                   -- like the local state communication was really
                   -- that critical
                   ssloop.loop():poll(0)

                   -- XXX - should we check if we need to run the PA
                   -- alg?  If so, we should consult both SKV state,
                   -- and LSA state
                   local epa = get_elsa_pa()
                   epa:run()
                end)
end

print('hello from LUA')
