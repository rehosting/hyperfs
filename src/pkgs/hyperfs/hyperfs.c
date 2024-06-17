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

static struct options {
  const char *hyperfile_paths;
  const char *passthrough_path;
} options;

#define OPTION(t, p)                                                           \
  { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
    OPTION("--hyperfile-paths=%s", hyperfile_paths),
    OPTION("--passthrough-path=%s", passthrough_path),
};

#include "passthrough.c"

enum { READ, WRITE, IOCTL, GETATTR };

enum { DEV_MODE = S_IFREG | 0666, DIR_MODE = S_IFDIR | 0777 };

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
  volatile unsigned char x = 0;
  size_t i;
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

static int hyperfs_hypercall(struct hyperfs_data data) {
  void *s[] = {&data};
  page_in_hyperfs_data(&data);
  return hc(MAGIC_VALUE, s, 1);
}

static void for_each_path(void (*func)(const char *path,
                                       const char *target_path, void *data),
                          const char *target_path, void *data) {
  char *hyperfile_paths_mut = strdup(options.hyperfile_paths);
  char *scratch = hyperfile_paths_mut;

  for (;;) {
    char *token = strtok(scratch, ":");
    if (!token) {
      break;
    }
    func(token, target_path, data);
    scratch = NULL;
  }

  free(hyperfile_paths_mut);
}

static void lookup_mode_func(const char *path, const char *target_path,
                             void *data) {
  int *mode = data;

  if (!strcmp(path, target_path)) {
    *mode = DEV_MODE;
  } else if (!strcmp(target_path, "/") ||
             (!strncmp(path, target_path, strlen(target_path)) &&
              path[strlen(target_path)] == '/')) {
    *mode = DIR_MODE;
  }
}

static int lookup_mode(const char *path) {
  int mode = -1;
  for_each_path(lookup_mode_func, path, &mode);
  return mode;
}

static bool exists(const char *path) { return lookup_mode(path) >= 0; }

static int hyperfs_open(const char *path, struct fuse_file_info *fi) {
  fi->direct_io = 1;
  if (exists(path)) {
    return 0;
  } else {
    return xmp_open(path, fi);
  }
}

static int hyperfs_getattr(const char *path, struct stat *st,
                           struct fuse_file_info *fi) {
  (void)fi;
  int mode = lookup_mode(path);

  if (mode >= 0) {
    memset(st, 0, sizeof(struct stat));
    st->st_nlink = !strcmp(path, "/") ? 2 : 1;
    st->st_mode = mode;
    hyperfs_hypercall((struct hyperfs_data){
      .type = GETATTR,
      .path = path,
      .getattr.size = &st->st_size,
    });
    return 0;
  } else {
    return xmp_getattr(path, st, fi);
  }
}

static void readdir_func(const char *path, const char *target_path,
                         void *data) {
  void **ptrs = data;
  fuse_fill_dir_t filler = ptrs[0];
  void *buf = ptrs[1];
  if (!strcmp(target_path, "/")) {
    target_path = "";
  }
  size_t target_path_len = strlen(target_path);
  if (!strncmp(path, target_path, target_path_len) &&
      path[target_path_len] == '/') {
    char *file_name = strdup(&path[target_path_len + 1]);
    *strchrnul(file_name, '/') = 0;
    // Avoid duplicates with real underlying filesystem
    char igloo_path[PATH_MAX];
    snprintf(igloo_path, PATH_MAX, "%s%s/%s", options.passthrough_path,
             target_path, file_name);
    if (access(igloo_path, F_OK)) {
      filler(buf, file_name, NULL, 0, 0);
    }
    free(file_name);
  }
}

static int hyperfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi,
                           enum fuse_readdir_flags flags) {
  int res;
  void *ptrs[] = {filler, buf};

  (void)offset;
  (void)fi;
  (void)flags;

  res = xmp_readdir(path, buf, filler, offset, fi, flags);
  if (lookup_mode(path) != DIR_MODE) {
    return res;
  }
  for_each_path(readdir_func, path, ptrs);
  return 0;
}

static int hyperfs_truncate(const char *path, off_t offset,
                            struct fuse_file_info *fi) {
  if (exists(path)) {
    return 0;
  } else {
    return xmp_truncate(path, offset, fi);
  }
}

static int hyperfs_read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
  (void)fi;

  return lookup_mode(path) == DEV_MODE
    ? hyperfs_hypercall((struct hyperfs_data){
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
  (void)fi;

  return lookup_mode(path) == DEV_MODE
    ? hyperfs_hypercall((struct hyperfs_data){
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
  (void)arg;
  (void)fi;
  (void)flags;

  return lookup_mode(path) == DEV_MODE
    ? hyperfs_hypercall((struct hyperfs_data){
      .type = IOCTL,
      .path = path,
      .ioctl.cmd = cmd,
      .ioctl.data = data_,
    })
    : xmp_ioctl(path, cmd, arg, fi, flags, data_);
}

static int hyperfs_readlink(const char *path, char *buf, size_t size) {
  if (exists(path)) {
    return -EINVAL;
  } else if (!strcmp(path, "/proc/self")) {
    snprintf(buf, size, "/proc/%d", fuse_get_context()->pid);
    return 0;
  } else {
    return xmp_readlink(path, buf, size);
  }
}

static int hyperfs_release(const char *path, struct fuse_file_info *fi) {
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

int main(int argc, char *argv[]) {
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  if (fuse_opt_parse(&args, &options, option_spec, NULL)) {
    return 1;
  }
  if (!options.hyperfile_paths) {
    fputs("error: missing --hyperfile-paths\n", stderr);
  }
  if (!options.passthrough_path) {
    fputs("error: missing --passthrough-path\n", stderr);
  }
  return fuse_main(args.argc, args.argv, &fops, NULL);
}
