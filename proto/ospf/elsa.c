/*
 * $Id: elsa.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Copyright (c) 2012 cisco Systems, Inc.
 *
 * Created:       Wed Aug  1 14:01:30 2012 mstenber
 * Last modified: Wed May 22 14:41:43 2013 mstenber
 * Edit time:     62 min
 *
 */

#include <stdlib.h>

#include "elsa_internal.h"
#include "lauxlib.h"
#include "lualib.h"

extern int luaopen_elsac(lua_State* L);

elsa elsa_create(elsa_client client, const char *elsa_path)
{
  elsa e;
  int r;

  if (!elsa_path)
    {
      ELSA_DEBUG("elsa path not specified -> skipping");
      return NULL;
    }

  e = elsai_calloc(client, sizeof(*e));
  e->client = client;
  e->l = luaL_newstate();
  luaL_openlibs(e->l);
  luaopen_elsac(e->l);
  if ((r = luaL_loadfile(e->l, elsa_path)))
    {
      ELSA_ERROR("error %d in lua loadfile: %s", r, lua_tostring(e->l, -1));
      lua_pop(e->l, 1);
      // is this fatal? hmm
      abort();
    }

  if ((r = lua_pcall(e->l, 0, 0, 0)))
    {
      ELSA_ERROR("error %d in lua pcall: %s", r, lua_tostring(e->l, -1));
      lua_pop(e->l, 1);
      // is this fatal? hmm
      abort();
    }

  ELSA_DEBUG("created elsa %p for client %p", e, client);
  return e;
}

void elsa_destroy(elsa e)
{
  if (!e)
    return;

  lua_close(e->l);
  elsai_free(e->client, e);
  ELSA_DEBUG("destroyed elsa %p", e);
}

/* LUA-specific magic - this way we don't need to worry about encoding
 * the elsa correctly as elsa_dispatch parameter. */
static elsa active_elsa;

void elsa_dispatch(elsa e)
{
  int r;

  if (!e)
    return;

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

static void dispatch_lsa_callback(elsa e, elsa_lsa lsa, const char *cb_name)
{
  int r;

  if (!e)
    return;

  active_elsa = e;
  active_elsa_lsa = lsa;
  /* Call LUA */
  lua_getglobal(e->l, cb_name);
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

void elsa_notify_changed_lsa(elsa e, elsa_lsa lsa)
{
  dispatch_lsa_callback(e, lsa, "elsa_notify_changed_lsa");
}

void elsa_notify_deleting_lsa(elsa e, elsa_lsa lsa)
{
  dispatch_lsa_callback(e, lsa, "elsa_notify_deleting_lsa");
}

void elsa_notify_duplicate_lsa(elsa e, elsa_lsa lsa)
{
  dispatch_lsa_callback(e, lsa, "elsa_notify_duplicate_lsa");
}

elsa_lsa elsa_active_lsa_get(void)
{
  return active_elsa_lsa;
}

void elsa_log_string(const char *string)
{
  log(L_TRACE "%s", string);
}
