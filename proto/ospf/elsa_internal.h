/*
 * $Id: elsa_internal.h $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *         Benjamin Paterson <paterson.b@gmail.com>
 *
 * Copyright (c) 2012 cisco Systems, Inc.
 *
 * Created:       Wed Aug  1 14:23:23 2012 mstenber
 * Last modified: Thu Nov 15 12:13:18 2012 mstenber
 * Edit time:     18 min
 *
 */

#ifndef ELSA_INTERNAL_H
#define ELSA_INTERNAL_H

#include <assert.h>
#include <lua.h>

#include "elsa.h"

struct elsa_struct {
  elsa_client client;

  struct elsa_platform_struct platform;

  lua_State *l;
};

#endif /* ELSA_INTERNAL_H */
