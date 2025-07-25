/*
 * HyperFS: Hypervisor-managed filesystem
 * Copyright (C) 2024 Massachusetts Institute of Technology
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE
 */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 35
#define MAGIC_VALUE 0x51ec3692 // crc32("hyperfs")
#define PACKED __attribute__((packed))

#include <dirent.h>
#include <errno.h>
#include <fuse.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "hypercall.h"

static size_t num_hyperfiles;
static char **hyperfile_paths;

static struct options { const char *passthrough_path; } options;

#define OPTION(t, p)                                                           \
  { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
    OPTION("--passthrough-path=%s", passthrough_path),
    FUSE_OPT_END,
};

static void trace(const char *fmt, ...) {
  const char *path = getenv("HYPERFS_TRACE_PATH");
  if (!path) {
    return;
  }

  FILE *file = fopen(path, "a");
  if (!file) {
    return;
  }

  va_list args;
  va_start(args, fmt);

  vfprintf(file, fmt, args);
  fputc('\n', file);

  va_end(args);

  fclose(file);
}

#include "passthrough.c"

enum { HYP_FILE_OP, HYP_GET_NUM_HYPERFILES, HYP_GET_HYPERFILE_PATHS };

enum { READ, WRITE, IOCTL, GETATTR };

enum { DEV_MODE = S_IFREG | 0666, DIR_MODE = S_IFDIR | 0777 };

enum { HYPERFILE_PATH_MAX = 1024 };

struct hyperfs_data {
  int type;
  const char *path;
  union {
    struct {
      char *buf;
      size_t size;
      off_t offset;
    } PACKED read;
    struct {
      const char *buf;
      size_t size;
      off_t offset;
    } PACKED write;
    struct {
      unsigned int cmd;
      void *data;
    } PACKED ioctl;
    struct {
      off_t *size;
    } PACKED getattr;
  } PACKED;
} PACKED;

static void page_in_hyperfs_data(struct hyperfs_data *data) {
  trace("%s(%p)", __func__, data);

  volatile unsigned char x = 0;
  size_t i;
  for (i = 0; i < sizeof(*data); i++) {
    x += ((unsigned char *)data)[i];
  }
  for (i = 0; data->path[i]; i++) {
    x += data->path[i];
  }
  switch (data->type) {
  case READ:
    for (i = 0; i < data->read.size; i++) {
      x += data->read.buf[i];
    }
    break;
  case WRITE:
    for (i = 0; i < data->write.size; i++) {
      x += data->write.buf[i];
    }
    break;
  }
}

static int hyp_file_op(struct hyperfs_data data) {
  trace("%s(data)", __func__);
  unsigned long err = RETRY;
  do {
    page_in_hyperfs_data(&data);
    err = igloo_hypercall2(MAGIC_VALUE, HYP_FILE_OP, (unsigned long)&data);
  } while (err == RETRY);
  return err;
}

static int lookup_mode(const char *path) {
  trace("%s(%s)", __func__, path);
  int mode = -1;
  for (size_t i = 0; i < num_hyperfiles; i++) {
    const char *hf_path = hyperfile_paths[i];
    if (!strcmp(path, hf_path)) {
      mode = DEV_MODE;
    } else if (!strcmp(path, "/") || (!strncmp(path, hf_path, strlen(path)) &&
                                      hf_path[strlen(path)] == '/')) {
      mode = DIR_MODE;
    }
  }
  return mode;
}

static bool exists(const char *path) { return lookup_mode(path) >= 0; }

static int hyperfs_open(const char *path, struct fuse_file_info *fi) {
  trace("%s(%s, %p)", __func__, path, fi);
  fi->direct_io = 1;
  if (exists(path)) {
    return 0;
  } else {
    return xmp_open(path, fi);
  }
}

// Return whether a path is /proc/self or /proc/PID
static bool is_proc_pid_path(const char *path)
{
  int pid, len;
  bool is_proc_self = !strcmp(path, "/proc/self");
  bool is_proc_pid = sscanf(path, "/proc/%d%n", &pid, &len) == 1 && len == strlen(path);
  return is_proc_self || is_proc_pid;
}

static int hyperfs_getattr(const char *path, struct stat *st,
                           struct fuse_file_info *fi) {
  trace("%s(%s, st=%p, fi=%p)", __func__, path, st, fi);

  memset(st, 0, sizeof(struct stat));
  st->st_nlink = !strcmp(path, "/") ? 2 : 1;

  int mode = lookup_mode(path);

  if (mode >= 0) {
    st->st_mode = mode;
    if (mode == DEV_MODE) {
      hyp_file_op((struct hyperfs_data){
          .type = GETATTR,
          .path = path,
          .getattr.size = &st->st_size,
      });
    }
    return 0;
  } else if (is_proc_pid_path(path)) {
    st->st_mode = S_IFLNK | 0777;
    return 0;
  } else {
    return xmp_getattr(path, st, fi);
  }
}

static int hyperfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi,
                           enum fuse_readdir_flags flags) {
  trace("%s(%s, buf=%p, filler=%p, offset=%ld, fi=%p)", __func__, path, buf, filler, (long) offset, fi);

  int res;

  res = xmp_readdir(path, buf, filler, offset, fi, flags);
  if (lookup_mode(path) != DIR_MODE) {
    return res;
  }

  for (size_t i = 0; i < num_hyperfiles; i++) {
    const char *hf_path = hyperfile_paths[i];
    if (!strcmp(path, "/")) {
      path = "";
    }
    size_t path_len = strlen(path);
    if (!strncmp(path, hf_path, path_len) && hf_path[path_len] == '/') {
      char *file_name = strdup(&hf_path[path_len + 1]);
      *strchrnul(file_name, '/') = 0;
      // Avoid duplicates with real underlying filesystem
      char igloo_path[PATH_MAX];
      snprintf(igloo_path, PATH_MAX, "%s%s/%s", options.passthrough_path, path,
               file_name);
      if (access(igloo_path, F_OK)) {
        filler(buf, file_name, NULL, 0, 0);
      }
      free(file_name);
    }
  }

  return 0;
}

static int hyperfs_truncate(const char *path, off_t offset,
                            struct fuse_file_info *fi) {
  trace("%s(%s, offset=%ld, fi=%p)", __func__, path, (long) offset, fi);
  if (exists(path)) {
    return 0;
  } else {
    return xmp_truncate(path, offset, fi);
  }
}

static int hyperfs_read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
  trace("%s(%s, buf=%p, size=%zu, offset=%ld, fi=%p)", __func__, path, buf, size, (long) offset, fi);

  return lookup_mode(path) == DEV_MODE ? hyp_file_op((struct hyperfs_data){
                                             .type = READ,
                                             .path = path,
                                             .read.buf = buf,
                                             .read.size = size,
                                             .read.offset = offset,
                                         })
                                       : xmp_read(path, buf, size, offset, fi);
}

static int hyperfs_write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {
  trace("%s(%s, buf=%p, size=%zu, offset=%ld, fi=%p)", __func__, path, buf, size, (long) offset, fi);

  return lookup_mode(path) == DEV_MODE ? hyp_file_op((struct hyperfs_data){
                                             .type = WRITE,
                                             .path = path,
                                             .write.buf = buf,
                                             .write.size = size,
                                             .write.offset = offset,
                                         })
                                       : xmp_write(path, buf, size, offset, fi);
}

static int hyperfs_ioctl(const char *path, unsigned int cmd, void *arg,
                         struct fuse_file_info *fi, unsigned int flags,
                         void *data_) {
  trace("%s(%s, cmd=%u, arg=%p, fi=%p, flags=%x, data=%p)", __func__, path, cmd, arg, fi, flags, data_);
  return lookup_mode(path) == DEV_MODE
             ? hyp_file_op((struct hyperfs_data){
                   .type = IOCTL,
                   .path = path,
                   .ioctl.cmd = cmd,
                   .ioctl.data = data_,
               })
             : xmp_ioctl(path, cmd, arg, fi, flags, data_);
}

static int hyperfs_readlink(const char *path, char *buf, size_t size) {
  trace("%s(%s, buf=%s, size=%zu)", __func__, path, buf, size);
  if (exists(path)) {
    return -EINVAL;
  } else if (is_proc_pid_path(path)) {
    snprintf(buf, size, "%s%s", options.passthrough_path, path);
    return 0;
  } else {
    return xmp_readlink(path, buf, size);
  }
}

static int hyperfs_release(const char *path, struct fuse_file_info *fi) {
  trace("%s(%s, fi=%p)", __func__, path, fi);
  if (exists(path)) {
    return 0;
  } else {
    return xmp_release(path, fi);
  }
}

static const struct fuse_operations fops = {
    .open = hyperfs_open,
    .getattr = hyperfs_getattr,
    .readdir = hyperfs_readdir,
    .truncate = hyperfs_truncate,
    .read = hyperfs_read,
    .write = hyperfs_write,
    .ioctl = hyperfs_ioctl,

    .readlink = hyperfs_readlink,
    .release = hyperfs_release,

    .init = xmp_init,
    .mknod = xmp_mknod,
    .mkdir = xmp_mkdir,
    .unlink = xmp_unlink,
    .rmdir = xmp_rmdir,
    .symlink = xmp_symlink,
    .rename = xmp_rename,
    .link = xmp_link,
    .chmod = xmp_chmod,
    .chown = xmp_chown,
    .create = xmp_create,
    .statfs = xmp_statfs,
    .fsync = xmp_fsync,
};

static void load_hyperfile_paths(void) {
  trace("%s()", __func__);
  hc(HYP_GET_NUM_HYPERFILES, (void *[]){&num_hyperfiles}, 1);
  hyperfile_paths = calloc(num_hyperfiles, sizeof(*hyperfile_paths));
  for (size_t i = 0; i < num_hyperfiles; i++) {
    hyperfile_paths[i] = calloc(HYPERFILE_PATH_MAX, 1);
  }
  hc(HYP_GET_HYPERFILE_PATHS, (void **)hyperfile_paths, num_hyperfiles);
}

int main(int argc, char *argv[]) {
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  if (fuse_opt_parse(&args, &options, option_spec, NULL)) {
    return 1;
  }
  if (!options.passthrough_path) {
    fputs("error: missing --passthrough-path\n", stderr);
  }
  load_hyperfile_paths();
  return fuse_main(args.argc, args.argv, &fops, NULL);
}
