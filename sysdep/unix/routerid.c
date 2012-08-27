/*
 *	BIRD Internet Routing Daemon -- Router ID storage
 *
 *	(c) 2012 Benjamin Paterson <benjamin@paterson.fr>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

/**
 * DOC: Router ID file
 * Essentially a stripped-down version of sysdep/unix/log.c.
 *
 * Allows reading/writing Router ID to a file.
 * To write the RID to file, we use BIRD's bvsnprintf to easily
 * format the RID and write it to a temporary buffer, then
 * we use standard C library functions to write the buffer to file.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include "nest/bird.h"
#include "lib/string.h"

#define STD_RID_P_LENGTH 15
#define RID_BUFFER_SIZE 100
static char rid_buffer[RID_BUFFER_SIZE];
static char *rid_buffer_pos;
static int rid_buffer_remains;

/**
 * rid_reset - reset the rid buffer
 *
 * This function resets a rid buffer and discards buffered
 * messages. Should be used before a preparing a rid entry with ridn().
 */
static void
rid_reset(void)
{
  rid_buffer_pos = rid_buffer;
  rid_buffer_remains = RID_BUFFER_SIZE;
  rid_buffer[0] = 0;
}

/**
 * rid_commit - commit a rid line
 *
 * This function writes a message prepared in the log buffer to the
 * rid file (as specified in the configuration). The rid buffer is
 * reset after that. The rid message is a full line, rid_commit()
 * terminates it.
 */
static void
rid_commit(void *f)
{
  if (f)
  {
    fputs(rid_buffer, f);
    fputc('\n', f);
    fflush(f);
  }

  rid_reset();
}

static void
rid_print(const char *msg, va_list args)
{
  int i;

  if (rid_buffer_remains == 0)
    return;

  i=bvsnprintf(rid_buffer_pos, rid_buffer_remains, msg, args);
  if (i < 0)
    {
      bsprintf(rid_buffer + RID_BUFFER_SIZE - 100, " ... <too long>");
      rid_buffer_remains = 0;
      return;
    }

  rid_buffer_pos += i;
  rid_buffer_remains -= i;
}

/**
 * ridn - prepare a partial message in the rid buffer
 * @msg: printf-like formatting string (without message class information)
 *
 * This function formats a message according to the format string @msg
 * and adds it to the rid buffer. Messages in the rid buffer are
 * written when the buffer is flushed using rid_commit() function. The
 * message should not contain |\n|, rid_commit() also terminates a
 * line.
 */
static void
ridn(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  rid_print(msg, args);
  va_end(args);
}

u32 read_rid(const char *filename)
{
  u32 ret = 0;
  char *pos;
  char buf[STD_RID_P_LENGTH + 1];
  FILE *f = fopen(filename, "r");

  if (!f)
    return 0;
  if(fgets(buf, STD_RID_P_LENGTH + 1, f))
  {
    /* get rid of trailing newline */
    if ((pos=strchr(buf, '\n')) != NULL)
      *pos = '\0';

    if(!ipv4_pton_u32(buf, &ret))
      ret = 0;
  }
  fclose(f);
  return ret;
}

void write_rid(const char *filename, u32 rid)
{
  FILE *f = fopen(filename, "w");

  rid_reset();
  ridn("%R", rid);
  rid_commit(f);
  fclose(f);
}
