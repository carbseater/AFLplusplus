/*
   american fuzzy lop++ - common routines
   --------------------------------------

   Originally written by Michal Zalewski

   Now maintained by Marc Heuse <mh@mh-sec.de>,
                        Heiko Eißfeldt <heiko.eissfeldt@hexco.de> and
                        Andrea Fioraldi <andreafioraldi@gmail.com>

   Copyright 2016, 2017 Google Inc. All rights reserved.
   Copyright 2019-2020 AFLplusplus Project. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

   Gather some functions common to multiple executables

   - detect_file_args

 */

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <math.h>

#include "debug.h"
#include "alloc-inl.h"
#include "envs.h"
#include "common.h"

/* Detect @@ in args. */
#ifndef __glibc__
  #include <unistd.h>
#endif
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

u8  be_quiet = 0;
u8 *doc_path = "";
u8  last_intr = 0;

void detect_file_args(char **argv, u8 *prog_in, bool *use_stdin) {

  u32 i = 0;
  u8  cwd[PATH_MAX];
  if (getcwd(cwd, (size_t)sizeof(cwd)) == NULL) { PFATAL("getcwd() failed"); }

  /* we are working with libc-heap-allocated argvs. So do not mix them with
   * other allocation APIs like ck_alloc. That would disturb the free() calls.
   */
  while (argv[i]) {

    u8 *aa_loc = strstr(argv[i], "@@");

    if (aa_loc) {

      if (!prog_in) { FATAL("@@ syntax is not supported by this tool."); }

      *use_stdin = false;

      if (prog_in[0] != 0) {  // not afl-showmap special case

        u8 *n_arg;

        /* Be sure that we're always using fully-qualified paths. */

        *aa_loc = 0;

        /* Construct a replacement argv value. */

        if (prog_in[0] == '/') {

          n_arg = alloc_printf("%s%s%s", argv[i], prog_in, aa_loc + 2);

        } else {

          n_arg = alloc_printf("%s%s/%s%s", argv[i], cwd, prog_in, aa_loc + 2);

        }

        ck_free(argv[i]);
        argv[i] = n_arg;

      }

    }

    i++;

  }

  /* argvs are automatically freed at exit. */

}

/* duplicate the system argv so that
  we can edit (and free!) it later */

char **argv_cpy_dup(int argc, char **argv) {

  int i = 0;

  char **ret = ck_alloc((argc + 1) * sizeof(char *));
  if (unlikely(!ret)) { FATAL("Amount of arguments specified is too high"); }

  for (i = 0; i < argc; i++) {

    ret[i] = ck_strdup(argv[i]);

  }

  ret[i] = NULL;

  return ret;

}

/* frees all args in the given argv,
   previously created by argv_cpy_dup */

void argv_cpy_free(char **argv) {

  u32 i = 0;
  while (argv[i]) {

    ck_free(argv[i]);
    argv[i] = NULL;
    i++;

  }

  ck_free(argv);

}

/* Rewrite argv for QEMU. */

char **get_qemu_argv(u8 *own_loc, u8 **target_path_p, int argc, char **argv) {

  if (!unlikely(own_loc)) { FATAL("BUG: param own_loc is NULL"); }

  u8 *tmp, *cp = NULL, *rsl, *own_copy;

  char **new_argv = ck_alloc(sizeof(char *) * (argc + 4));
  if (unlikely(!new_argv)) { FATAL("Illegal amount of arguments specified"); }

  memcpy(&new_argv[3], &argv[1], (int)(sizeof(char *)) * (argc - 1));
  new_argv[argc + 3] = NULL;

  new_argv[2] = *target_path_p;
  new_argv[1] = "--";

  /* Now we need to actually find the QEMU binary to put in argv[0]. */

  tmp = getenv("AFL_PATH");

  if (tmp) {

    cp = alloc_printf("%s/afl-qemu-trace", tmp);

    if (access(cp, X_OK)) { FATAL("Unable to find '%s'", tmp); }

    *target_path_p = new_argv[0] = cp;
    return new_argv;

  }

  own_copy = ck_strdup(own_loc);
  rsl = strrchr(own_copy, '/');

  if (rsl) {

    *rsl = 0;

    cp = alloc_printf("%s/afl-qemu-trace", own_copy);
    ck_free(own_copy);

    if (!access(cp, X_OK)) {

      *target_path_p = new_argv[0] = cp;
      return new_argv;

    }

  } else {

    ck_free(own_copy);

  }

  if (!access(BIN_PATH "/afl-qemu-trace", X_OK)) {

    if (cp) { ck_free(cp); }
    *target_path_p = new_argv[0] = ck_strdup(BIN_PATH "/afl-qemu-trace");

    return new_argv;

  }

  SAYF("\n" cLRD "[-] " cRST
       "Oops, unable to find the 'afl-qemu-trace' binary. The binary must be "
       "built\n"
       "    separately by following the instructions in "
       "qemu_mode/README.md. "
       "If you\n"
       "    already have the binary installed, you may need to specify "
       "AFL_PATH in the\n"
       "    environment.\n\n"

       "    Of course, even without QEMU, afl-fuzz can still work with "
       "binaries that are\n"
       "    instrumented at compile time with afl-gcc. It is also possible to "
       "use it as a\n"
       "    traditional non-instrumented fuzzer by specifying '-n' in the "
       "command "
       "line.\n");

  FATAL("Failed to locate 'afl-qemu-trace'.");

}

/* Rewrite argv for Wine+QEMU. */

char **get_wine_argv(u8 *own_loc, u8 **target_path_p, int argc, char **argv) {

  if (!unlikely(own_loc)) { FATAL("BUG: param own_loc is NULL"); }

  u8 *tmp, *cp = NULL, *rsl, *own_copy;

  char **new_argv = ck_alloc(sizeof(char *) * (argc + 3));
  if (unlikely(!new_argv)) { FATAL("Illegal amount of arguments specified"); }

  memcpy(&new_argv[2], &argv[1], (int)(sizeof(char *)) * (argc - 1));
  new_argv[argc + 2] = NULL;

  new_argv[1] = *target_path_p;

  /* Now we need to actually find the QEMU binary to put in argv[0]. */

  tmp = getenv("AFL_PATH");

  if (tmp) {

    cp = alloc_printf("%s/afl-qemu-trace", tmp);

    if (access(cp, X_OK)) { FATAL("Unable to find '%s'", tmp); }

    ck_free(cp);

    cp = alloc_printf("%s/afl-wine-trace", tmp);

    if (access(cp, X_OK)) { FATAL("Unable to find '%s'", tmp); }

    *target_path_p = new_argv[0] = cp;
    return new_argv;

  }

  own_copy = ck_strdup(own_loc);
  rsl = strrchr(own_copy, '/');

  if (rsl) {

    *rsl = 0;

    cp = alloc_printf("%s/afl-qemu-trace", own_copy);

    if (cp && !access(cp, X_OK)) {

      ck_free(cp);

      cp = alloc_printf("%s/afl-wine-trace", own_copy);

      if (!access(cp, X_OK)) {

        *target_path_p = new_argv[0] = cp;
        return new_argv;

      }

    }

    ck_free(own_copy);

  } else {

    ck_free(own_copy);

  }

  u8 *ncp = BIN_PATH "/afl-qemu-trace";

  if (!access(ncp, X_OK)) {

    ncp = BIN_PATH "/afl-wine-trace";

    if (!access(ncp, X_OK)) {

      *target_path_p = new_argv[0] = ck_strdup(ncp);
      return new_argv;

    }

  }

  SAYF("\n" cLRD "[-] " cRST
       "Oops, unable to find the '%s' binary. The binary must be "
       "built\n"
       "    separately by following the instructions in "
       "qemu_mode/README.md. "
       "If you\n"
       "    already have the binary installed, you may need to specify "
       "AFL_PATH in the\n"
       "    environment.\n\n"

       "    Of course, even without QEMU, afl-fuzz can still work with "
       "binaries that are\n"
       "    instrumented at compile time with afl-gcc. It is also possible to "
       "use it as a\n"
       "    traditional non-instrumented fuzzer by specifying '-n' in the "
       "command "
       "line.\n",
       ncp);

  FATAL("Failed to locate '%s'.", ncp);

}

/* Find binary, used by analyze, showmap, tmin
   @returns the path, allocating the string */

u8 *find_binary(u8 *fname) {

  // TODO: Merge this function with check_binary of afl-fuzz-init.c

  u8 *env_path = NULL;
  u8 *target_path = NULL;

  struct stat st;

  if (unlikely(!fname)) { FATAL("No binary supplied"); }

  if (strchr(fname, '/') || !(env_path = getenv("PATH"))) {

    target_path = ck_strdup(fname);

    if (stat(target_path, &st) || !S_ISREG(st.st_mode) ||
        !(st.st_mode & 0111) || st.st_size < 4) {

      ck_free(target_path);
      FATAL("Program '%s' not found or not executable", fname);

    }

  } else {

    while (env_path) {

      u8 *cur_elem, *delim = strchr(env_path, ':');

      if (delim) {

        cur_elem = ck_alloc(delim - env_path + 1);
        if (unlikely(!cur_elem)) {

          FATAL(
              "Unexpected overflow when processing ENV. This should never "
              "happend.");

        }

        memcpy(cur_elem, env_path, delim - env_path);
        delim++;

      } else {

        cur_elem = ck_strdup(env_path);

      }

      env_path = delim;

      if (cur_elem[0]) {

        target_path = alloc_printf("%s/%s", cur_elem, fname);

      } else {

        target_path = ck_strdup(fname);

      }

      ck_free(cur_elem);

      if (!stat(target_path, &st) && S_ISREG(st.st_mode) &&
          (st.st_mode & 0111) && st.st_size >= 4) {

        break;

      }

      ck_free(target_path);
      target_path = NULL;

    }

    if (!target_path) {

      FATAL("Program '%s' not found or not executable", fname);

    }

  }

  return target_path;

}

void check_environment_vars(char **envp) {

  if (be_quiet) { return; }

  int   index = 0, issue_detected = 0;
  char *env, *val;
  while ((env = envp[index++]) != NULL) {

    if (strncmp(env, "ALF_", 4) == 0 || strncmp(env, "_ALF", 4) == 0 ||
        strncmp(env, "__ALF", 5) == 0 || strncmp(env, "_AFL", 4) == 0 ||
        strncmp(env, "__AFL", 5) == 0) {

      WARNF("Potentially mistyped AFL environment variable: %s", env);
      issue_detected = 1;

    } else if (strncmp(env, "AFL_", 4) == 0) {

      int i = 0, match = 0;
      while (match == 0 && afl_environment_variables[i] != NULL) {

        if (strncmp(env, afl_environment_variables[i],
                    strlen(afl_environment_variables[i])) == 0 &&
            env[strlen(afl_environment_variables[i])] == '=') {

          match = 1;
          if ((val = getenv(afl_environment_variables[i])) && !*val) {

            WARNF(
                "AFL environment variable %s defined but is empty, this can "
                "lead to unexpected consequences",
                afl_environment_variables[i]);
            issue_detected = 1;

          }

        } else {

          i++;

        }

      }

      i = 0;
      while (match == 0 && afl_environment_deprecated[i] != NULL) {

        if (strncmp(env, afl_environment_deprecated[i],
                    strlen(afl_environment_deprecated[i])) == 0 &&
            env[strlen(afl_environment_deprecated[i])] == '=') {

          match = 1;

          WARNF("AFL environment variable %s is deprecated!",
                afl_environment_deprecated[i]);
          issue_detected = 1;

        } else {

          i++;

        }

      }

      if (match == 0) {

        WARNF("Mistyped AFL environment variable: %s", env);
        issue_detected = 1;

      }

    }

  }

  if (issue_detected) { sleep(2); }

}

char *get_afl_env(char *env) {

  char *val;

  if ((val = getenv(env)) != NULL) {

    if (!be_quiet) {

      OKF("Loaded environment variable %s with value %s", env, val);

    }

  }

  return val;

}

/* Read mask bitmap from file. This is for the -B option. */

void read_bitmap(u8 *fname, u8 *map, size_t len) {

  s32 fd = open(fname, O_RDONLY);

  if (fd < 0) { PFATAL("Unable to open '%s'", fname); }

  ck_read(fd, map, len, fname);

  close(fd);

}

u64 get_cur_time(void) {

  struct timeval  tv;
  struct timezone tz;

  gettimeofday(&tv, &tz);

  return (tv.tv_sec * 1000ULL) + (tv.tv_usec / 1000);

}

/* Get unix time in microseconds */

u64 get_cur_time_us(void) {

  struct timeval  tv;
  struct timezone tz;

  gettimeofday(&tv, &tz);

  return (tv.tv_sec * 1000000ULL) + tv.tv_usec;

}

/* Describe integer. The buf should be
   at least 6 bytes to fit all ints we randomly see.
   Will return buf for convenience. */

u8 *stringify_int(u8 *buf, size_t len, u64 val) {
\
#define CHK_FORMAT(_divisor, _limit_mult, _fmt, _cast)     \
  do {                                                     \
                                                           \
    if (val < (_divisor) * (_limit_mult)) {                \
                                                           \
      snprintf(buf, len, _fmt, ((_cast)val) / (_divisor)); \
      return buf;                                          \
                                                           \
    }                                                      \
                                                           \
  } while (0)

  /* 0-9999 */
  CHK_FORMAT(1, 10000, "%llu", u64);

  /* 10.0k - 99.9k */
  CHK_FORMAT(1000, 99.95, "%0.01fk", double);

  /* 100k - 999k */
  CHK_FORMAT(1000, 1000, "%lluk", u64);

  /* 1.00M - 9.99M */
  CHK_FORMAT(1000 * 1000, 9.995, "%0.02fM", double);

  /* 10.0M - 99.9M */
  CHK_FORMAT(1000 * 1000, 99.95, "%0.01fM", double);

  /* 100M - 999M */
  CHK_FORMAT(1000 * 1000, 1000, "%lluM", u64);

  /* 1.00G - 9.99G */
  CHK_FORMAT(1000LL * 1000 * 1000, 9.995, "%0.02fG", double);

  /* 10.0G - 99.9G */
  CHK_FORMAT(1000LL * 1000 * 1000, 99.95, "%0.01fG", double);

  /* 100G - 999G */
  CHK_FORMAT(1000LL * 1000 * 1000, 1000, "%lluG", u64);

  /* 1.00T - 9.99G */
  CHK_FORMAT(1000LL * 1000 * 1000 * 1000, 9.995, "%0.02fT", double);

  /* 10.0T - 99.9T */
  CHK_FORMAT(1000LL * 1000 * 1000 * 1000, 99.95, "%0.01fT", double);

  /* 100T+ */
  strncpy(buf, "infty", len);
  buf[len - 1] = '\0';

  return buf;

}

/* Describe float. Similar as int. */

u8 *stringify_float(u8 *buf, size_t len, double val) {

  if (val < 99.995) {

    snprintf(buf, len, "%0.02f", val);

  } else if (val < 999.95) {

    snprintf(buf, len, "%0.01f", val);

  } else {

    stringify_int(buf, len, (u64)val);

  }

  return buf;

}

/* Describe integer as memory size. */

u8 *stringify_mem_size(u8 *buf, size_t len, u64 val) {

  /* 0-9999 */
  CHK_FORMAT(1, 10000, "%llu B", u64);

  /* 10.0k - 99.9k */
  CHK_FORMAT(1024, 99.95, "%0.01f kB", double);

  /* 100k - 999k */
  CHK_FORMAT(1024, 1000, "%llu kB", u64);

  /* 1.00M - 9.99M */
  CHK_FORMAT(1024 * 1024, 9.995, "%0.02f MB", double);

  /* 10.0M - 99.9M */
  CHK_FORMAT(1024 * 1024, 99.95, "%0.01f MB", double);

  /* 100M - 999M */
  CHK_FORMAT(1024 * 1024, 1000, "%llu MB", u64);

  /* 1.00G - 9.99G */
  CHK_FORMAT(1024LL * 1024 * 1024, 9.995, "%0.02f GB", double);

  /* 10.0G - 99.9G */
  CHK_FORMAT(1024LL * 1024 * 1024, 99.95, "%0.01f GB", double);

  /* 100G - 999G */
  CHK_FORMAT(1024LL * 1024 * 1024, 1000, "%llu GB", u64);

  /* 1.00T - 9.99G */
  CHK_FORMAT(1024LL * 1024 * 1024 * 1024, 9.995, "%0.02f TB", double);

  /* 10.0T - 99.9T */
  CHK_FORMAT(1024LL * 1024 * 1024 * 1024, 99.95, "%0.01f TB", double);

#undef CHK_FORMAT

  /* 100T+ */
  strncpy(buf, "infty", len - 1);
  buf[len - 1] = '\0';

  return buf;

}

/* Describe time delta as string.
   Returns a pointer to buf for convenience. */

u8 *stringify_time_diff(u8 *buf, size_t len, u64 cur_ms, u64 event_ms) {

  u64 delta;
  s32 t_d, t_h, t_m, t_s;
  u8  val_buf[STRINGIFY_VAL_SIZE_MAX];

  if (!event_ms) {

    snprintf(buf, len, "none seen yet");

  } else {

    delta = cur_ms - event_ms;

    t_d = delta / 1000 / 60 / 60 / 24;
    t_h = (delta / 1000 / 60 / 60) % 24;
    t_m = (delta / 1000 / 60) % 60;
    t_s = (delta / 1000) % 60;

    stringify_int(val_buf, sizeof(val_buf), t_d);
    snprintf(buf, len, "%s days, %d hrs, %d min, %d sec", val_buf, t_h, t_m,
             t_s);

  }

  return buf;

}

/* Unsafe Describe integer. The buf sizes are not checked.
   This is unsafe but fast.
   Will return buf for convenience. */

u8 *u_stringify_int(u8 *buf, u64 val) {
\
#define CHK_FORMAT(_divisor, _limit_mult, _fmt, _cast) \
  do {                                                 \
                                                       \
    if (val < (_divisor) * (_limit_mult)) {            \
                                                       \
      sprintf(buf, _fmt, ((_cast)val) / (_divisor));   \
      return buf;                                      \
                                                       \
    }                                                  \
                                                       \
  } while (0)

  /* 0-9999 */
  CHK_FORMAT(1, 10000, "%llu", u64);

  /* 10.0k - 99.9k */
  CHK_FORMAT(1000, 99.95, "%0.01fk", double);

  /* 100k - 999k */
  CHK_FORMAT(1000, 1000, "%lluk", u64);

  /* 1.00M - 9.99M */
  CHK_FORMAT(1000 * 1000, 9.995, "%0.02fM", double);

  /* 10.0M - 99.9M */
  CHK_FORMAT(1000 * 1000, 99.95, "%0.01fM", double);

  /* 100M - 999M */
  CHK_FORMAT(1000 * 1000, 1000, "%lluM", u64);

  /* 1.00G - 9.99G */
  CHK_FORMAT(1000LL * 1000 * 1000, 9.995, "%0.02fG", double);

  /* 10.0G - 99.9G */
  CHK_FORMAT(1000LL * 1000 * 1000, 99.95, "%0.01fG", double);

  /* 100G - 999G */
  CHK_FORMAT(1000LL * 1000 * 1000, 1000, "%lluG", u64);

  /* 1.00T - 9.99G */
  CHK_FORMAT(1000LL * 1000 * 1000 * 1000, 9.995, "%0.02fT", double);

  /* 10.0T - 99.9T */
  CHK_FORMAT(1000LL * 1000 * 1000 * 1000, 99.95, "%0.01fT", double);

  /* 100T+ */
  strcpy(buf, "infty");

  return buf;

}

/* Unsafe describe float. Similar as unsafe int. */

u8 *u_stringify_float(u8 *buf, double val) {

  if (val < 99.995) {

    sprintf(buf, "%0.02f", val);

  } else if (val < 999.95) {

    sprintf(buf, "%0.01f", val);

  } else if (unlikely(isnan(val) || isfinite(val))) {

    strcpy(buf, "999.9");

  } else {

    return u_stringify_int(buf, (u64)val);

  }

  return buf;

}

/* Unsafe describe integer as memory size. */

u8 *u_stringify_mem_size(u8 *buf, u64 val) {

  /* 0-9999 */
  CHK_FORMAT(1, 10000, "%llu B", u64);

  /* 10.0k - 99.9k */
  CHK_FORMAT(1024, 99.95, "%0.01f kB", double);

  /* 100k - 999k */
  CHK_FORMAT(1024, 1000, "%llu kB", u64);

  /* 1.00M - 9.99M */
  CHK_FORMAT(1024 * 1024, 9.995, "%0.02f MB", double);

  /* 10.0M - 99.9M */
  CHK_FORMAT(1024 * 1024, 99.95, "%0.01f MB", double);

  /* 100M - 999M */
  CHK_FORMAT(1024 * 1024, 1000, "%llu MB", u64);

  /* 1.00G - 9.99G */
  CHK_FORMAT(1024LL * 1024 * 1024, 9.995, "%0.02f GB", double);

  /* 10.0G - 99.9G */
  CHK_FORMAT(1024LL * 1024 * 1024, 99.95, "%0.01f GB", double);

  /* 100G - 999G */
  CHK_FORMAT(1024LL * 1024 * 1024, 1000, "%llu GB", u64);

  /* 1.00T - 9.99G */
  CHK_FORMAT(1024LL * 1024 * 1024 * 1024, 9.995, "%0.02f TB", double);

  /* 10.0T - 99.9T */
  CHK_FORMAT(1024LL * 1024 * 1024 * 1024, 99.95, "%0.01f TB", double);

#undef CHK_FORMAT

  /* 100T+ */
  strcpy(buf, "infty");

  return buf;

}

/* Unsafe describe time delta as string.
   Returns a pointer to buf for convenience. */

u8 *u_stringify_time_diff(u8 *buf, u64 cur_ms, u64 event_ms) {

  u64 delta;
  s32 t_d, t_h, t_m, t_s;
  u8  val_buf[STRINGIFY_VAL_SIZE_MAX];

  if (!event_ms) {

    sprintf(buf, "none seen yet");

  } else {

    delta = cur_ms - event_ms;

    t_d = delta / 1000 / 60 / 60 / 24;
    t_h = (delta / 1000 / 60 / 60) % 24;
    t_m = (delta / 1000 / 60) % 60;
    t_s = (delta / 1000) % 60;

    u_stringify_int(val_buf, t_d);
    sprintf(buf, "%s days, %d hrs, %d min, %d sec", val_buf, t_h, t_m, t_s);

  }

  return buf;

}

/* Reads the map size from ENV */
u32 get_map_size(void) {

  uint32_t map_size = MAP_SIZE;
  char *   ptr;

  if ((ptr = getenv("AFL_MAP_SIZE")) || (ptr = getenv("AFL_MAPSIZE"))) {

    map_size = atoi(ptr);
    if (map_size < 8 || map_size > (1 << 29)) {

      FATAL("illegal AFL_MAP_SIZE %u, must be between %u and %u", map_size, 8,
            1 << 29);

    }

    if (map_size % 8) { map_size = (((map_size >> 3) + 1) << 3); }

  }

  return map_size;

}

/* Create a stream file */

FILE *create_ffile(u8 *fn) {

  s32   fd;
  FILE *f;

  fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0600);

  if (fd < 0) { PFATAL("Unable to create '%s'", fn); }

  f = fdopen(fd, "w");

  if (!f) { PFATAL("fdopen() failed"); }

  return f;

}

/* Create a file */

s32 create_file(u8 *fn) {

  s32 fd;

  fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0600);

  if (fd < 0) { PFATAL("Unable to create '%s'", fn); }

  return fd;

}

