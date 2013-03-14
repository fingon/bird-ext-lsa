#!/usr/bin/env lua
-- -*-lua-*-
--
-- $Id: elsa.lua $
--
-- Author: Markus Stenberg <fingon@iki.fi>
--
-- Copyright (c) 2012 cisco Systems, Inc.
--
-- Created:       Wed Sep 26 23:01:06 2012 mstenber
-- Last modified: Thu Mar 14 13:40:19 2013 mstenber
-- Edit time:     178 min
--

-- override the print stmt

-- (Have to do this here, as some of the sub-modules require'd are
-- side-effect-free, and therefore will cache local copy of print
-- function)

function print(...)
   local l = mst.array_map({...}, tostring)
   elsac.elsa_log_string(string.format('[lua] %s', table.concat(l, "\t")))
end



require 'mst'
require 'ssloop'
require 'skv'
require 'elsa_pa'
require 'linux_if'

elsaw = mst.create_class{class='elsaw', mandatory={'c'}}

-- we don't need date prefix to log messages, as we already get
-- timestamps from Bird log facility
mst.enable_debug_date = false

function elsaw:init()
   -- something we need to do here?
   self.sn = 0

   -- calculate the hardware fingerprint
   local if_table = linux_if.if_table:new{shell=mst.execute_to_string}
   local m = if_table:read_ip_ipv6()
   local hwset = mst.set:new()
   
   for ifname, ifo in pairs(m)
   do
      local hwa, err = ifo:get_hwaddr()
      if hwa
      then
         hwset:insert(hwa)
      else
         self:d('unable to get hwaddr', ifname, err)
      end
   end

   -- must have at least one hw address
   mst.a(hwset:count() > 0)
   
   local hwl = hwset:keys()

   -- in-place sort 
   hwl:sort()
   self.hwf = hwl:join(' ')

   mst.d('got hwf', self.hwf)

end

function elsaw:iterate_lsa(rid, f, criteria)
   mst.a(criteria, 'criteria mandatory')
   mst.a(criteria.type, 'criteria.type mandatory')
   for lsa in elsai_lsas_by_type(self.c, criteria.type)
   do
      f(lsa)
   end
end

local _eif_cache

function elsaw:iterate_if(rid, f)
   for i in elsai_interfaces(self.c)
   do
      f(i)
   end
end

function elsaw:iterate_ifo_neigh(rid, ifo, f)
   local i = _eif_cache[ifo.index]
   mst.a(i, 'unable to find interface', ifo)
   local n = elsac.elsai_if_get_neigh(self.c, i)
   while n
   do
      local iid = elsac.elsai_neigh_get_iid(self.c, n)
      local rid = elsac.elsai_neigh_get_rid(self.c, n)
      f{iid=iid, rid=rid}
      n = elsac.elsai_neigh_get_next(self.c, n)
   end
end

function elsaw:originate_lsa(d)
   self:a(d.type == elsa_pa.AC_TYPE, 'support for other types missing')
   self:a(d.body, 'no body?!?')
   self.sn = self.sn + 1
   elsac.elsai_lsa_originate(self.c, d.type, 0, self.sn, d.body)
end

should_change_rid = false

function elsaw:change_rid()
   mst.d('should change rid')
   should_change_rid = true
end

function elsaw:get_hwf()
   return self.hwf
end

function elsaw:route_to_rid(rid0, rid)
   if rid0 == rid
   then
      return {}
   end
   local nh, ifname = elsac.elsai_route_to_rid(self.c, rid)
   local r = {nh=nh, ifname=ifname}
   return r
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
   _eif_cache[t.index] = i
   --mst.d(i, mst.repr(t))
   return t
end

function elsai_interfaces(c)
   -- clear the cache every time iteration starts 
   _eif_cache = {}
   return elsai_interfaces_iterator, c, nil
end

function wrap_lsa(l)
   local t = {}
   setmetatable(t, {l=l})
   t.type = elsac.elsai_lsa_get_type(l)
   t.rid = elsac.elsai_lsa_get_rid(l)
   t.lsid = elsac.elsai_lsa_get_lsid(l)
   t.age = elsac.elsai_lsa_get_age(l)
   t.body = elsac.elsai_lsa_get_body(l)
   return t
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
   local t = wrap_lsa(l)
   assert(t.type == type, "invalid type for lsa")
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
   print('router rid', rid)
end

local _elsa_pa = false

function get_elsa_pa()
   local e = elsac.elsa_active_get()
   --mst.d('got active', e)
   
   local c = e.client
   --mst.d('got client', c)
   
   local rid = elsac.elsai_get_rid(c)
   --mst.d('router rid', rid)

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

                   local epa = get_elsa_pa()

                   -- first off, take care of the rid change
                   -- outright if it seems necessary
                   if should_change_rid
                   then
                      mst.d('changing rid')
                      elsac.elsai_change_rid(epa.c)
                      should_change_rid = false
                      return
                   end
                   
                   -- XXX - should we check if we need to run the PA
                   -- alg?  If so, we should consult both SKV state,
                   -- and LSA state
                   epa:run()
                end)
end

function elsa_notify_duplicate_lsa()
   mst.d_xpcall(function ()
                   local epa = get_elsa_pa()
                   local lsa = wrap_lsa(elsac.elsa_active_lsa_get())

                   -- other LSAs we can't do anything about anyway
                   if lsa.type ~= elsa_pa.AC_TYPE
                   then
                      return
                   end
                   epa:check_conflict(lsa)
                end)
end

function elsa_notify_changed_lsa()
   mst.d_xpcall(function ()
                   local epa = get_elsa_pa()
                   local lsa = wrap_lsa(elsac.elsa_active_lsa_get())
                   epa:lsa_changed(lsa)
                end)

end

function elsa_notify_deleting_lsa()
   mst.d_xpcall(function ()
                   local epa = get_elsa_pa()
                   local lsa = wrap_lsa(elsac.elsa_active_lsa_get())
                   epa:lsa_deleting(lsa)
                end)

end

-- capture the io.stdout/stderr
function create_log_wrapper(prefix)
   return function (s)
      --elsac.elsa_log_string(string.format("%s %s", prefix, s))
      for i, v in ipairs(mst.string_split(s, "\n"))
      do
         local sl = mst.string_strip(v)
         if sl and #sl>0
         then
            elsac.elsa_log_string(string.format("%s %s", prefix, v))
         end
      end
   end
end

io.stdout = {write = create_log_wrapper("[lua-o]")}
io.stderr = {write = create_log_wrapper("[lua-e]")}

print('hello from LUA (print)')
elsac.elsa_log_string('hello from LUA (raw log)')
mst.d('hello from lua [debug]')
print('debug is', mst.enable_debug)

