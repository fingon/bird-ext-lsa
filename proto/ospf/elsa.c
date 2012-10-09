/*
 * $Id: elsa.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 14:01:30 2012 mstenber
 * Last modified: Tue Oct  9 16:11:07 2012 mstenber
 * Edit time:     47 min
 *
 */

#include <stdlib.h>

#include "elsa_internal.h"
#include "lauxlib.h"
#include "lualib.h"

extern int luaopen_elsac(lua_State* L);

elsa elsa_create(elsa_client client)
{
  elsa e;
  int r;

  e = elsai_calloc(client, sizeof(*e));
  e->client = client;
  if (elsai_ac_usp_get(client))
    {
      e->need_ac = true;
      e->need_originate_ac = true;
    }
  e->l = luaL_newstate();
  luaL_openlibs(e->l);
  luaopen_elsac(e->l);
  if ((r = luaL_loadfile(e->l, "elsa.lua")) ||
      (r = lua_pcall(e->l, 0, 0, 0))
      )
    {
      ELSA_ERROR("error %d in lua init: %s", r, lua_tostring(e->l, -1));
      lua_pop(e->l, 1);
      // is this fatal? hmm
      abort();
    }

  ELSA_DEBUG("created elsa %p for client %p", e, client);
  return e;
}

void elsa_destroy(elsa e)
{
  lua_close(e->l);
  elsai_free(e->client, e);
  ELSA_DEBUG("destroyed elsa %p", e);
}

void elsa_lsa_changed(elsa e, elsa_lsatype lsatype)
{
  assert(e);
  if (lsatype == LSA_T_AC)
    {
      ELSA_DEBUG("interesting LSA changed");
      e->need_ac = true;
    }
  else
    {
      ELSA_DEBUG("boring LSA changed (%x not %x)", lsatype, LSA_T_AC);
    }
}

void elsa_lsa_deleted(elsa e, elsa_lsatype lsatype)
{
  assert(e);
  e->need_ac = true;
}

bool elsa_supports_lsatype(elsa_lsatype lsatype)
{
  return lsatype == LSA_T_AC;
}

/* LUA-specific magic - this way we don't need to worry about encoding
 * the elsa correctly as elsa_dispatch parameter. */
static elsa active_elsa;

void elsa_dispatch(elsa e)
{
  int r;

  active_elsa = e;
  /* Call LUA */
  lua_getglobal(e->l, "elsa_dispatch");
  //lua_pushlightuserdata(e->l, (void *)e);
  //SWIG_Lua_NewPointerObj(e->l,e,SWIGTYPE_p_elsa_struct,0)

  if ((r = lua_pcall(e->l, 0, 0, 0)))
    {
      ELSA_ERROR("error %d in LUA lua_pcall: %s", r, lua_tostring(e->l, -1));
      lua_pop(e->l, 1);
      // is this fatal? hmm
      abort();
    }
  active_elsa = NULL;
}

elsa elsa_active_get(void)
{
  return active_elsa;
}

/* LUA-specific magic - this way we don't need to worry about encoding
 * the elsa correctly as elsa_duplicate_lsa_dispatch parameter. */
static elsa_lsa active_elsa_lsa;

void elsa_duplicate_lsa_dispatch(elsa e, elsa_lsa lsa)
{
  int r;

  active_elsa = e;
  active_elsa_lsa = lsa;
  /* Call LUA */
  lua_getglobal(e->l, "elsa_duplicate_lsa_dispatch");
  //lua_pushlightuserdata(e->l, (void *)e);
  //SWIG_Lua_NewPointerObj(e->l,e,SWIGTYPE_p_elsa_struct,0)

  if ((r = lua_pcall(e->l, 0, 0, 0)))
    {
      ELSA_ERROR("error %d in LUA lua_pcall: %s", r, lua_tostring(e->l, -1));
      lua_pop(e->l, 1);
      // is this fatal? hmm
      abort();
    }
  active_elsa = NULL;
  active_elsa_lsa = NULL;
}

elsa_lsa elsa_active_lsa_get(void)
{
  return active_elsa_lsa;
}

void elsa_log_string(const char *string)
{
  log(L_TRACE "%s", string);
}
