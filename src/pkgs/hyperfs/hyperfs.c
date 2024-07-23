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

#include <assert.h>
#include <cuse_lowlevel.h>
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
#include <sys/wait.h>
#include <unistd.h>

#include "hypercall.h"

static size_t num_hyperfiles;
static char **hyperfile_paths;
static dev_t *hyperfile_dev_nums;
static size_t cuse_hyperfile_index;

static struct options { const char *passthrough_path; } options;

#define OPTION(t, p)                                                           \
  { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
    OPTION("--passthrough-path=%s", passthrough_path),
};

#include "passthrough.c"

enum { HYP_FILE_OP, HYP_GET_NUM_HYPERFILES, HYP_GET_HYPERFILE_PATHS };

enum { READ, WRITE, IOCTL, GETATTR };

enum { DEV_MODE = S_IFCHR | 0666, DIR_MODE = S_IFDIR | 0777 };

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
      void **ptr;
      size_t *size;
      int *rw;
    } PACKED ioctl;
    struct {
      off_t *size;
    } PACKED getattr;
  } PACKED;
} PACKED;

static size_t min(size_t x, size_t y) {
  return x < y ? x : y;
}

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

static int hyp_file_op(struct hyperfs_data data) {
  void *s[] = {&data};
  page_in_hyperfs_data(&data);
  return hc(HYP_FILE_OP, s, 1);
}

static int lookup_mode(const char *path) {
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
    if (mode == DEV_MODE) {
      st->st_rdev = hyperfile_dev_nums[cuse_hyperfile_index];
      hyp_file_op((struct hyperfs_data){
          .type = GETATTR,
          .path = path,
          .getattr.size = &st->st_size,
      });
    }
    return 0;
  } else {
    return xmp_getattr(path, st, fi);
  }
}

static int hyperfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi,
                           enum fuse_readdir_flags flags) {
  int res;

  (void)offset;
  (void)fi;
  (void)flags;

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
  if (exists(path)) {
    return 0;
  } else {
    return xmp_truncate(path, offset, fi);
  }
}

static void clop_read(fuse_req_t req, size_t size, off_t off, struct fuse_file_info *fi) {
  (void)fi;

  char buf[1024] = { 0 };

  FILE *f = fopen("/dev/ttyAMA0", "w");
  fprintf(f, "HYPERFS: read with offset %d\n", (int)off);
  fflush(f);
  fclose(f);

  int nr = hyp_file_op((struct hyperfs_data){
    .type = READ,
    .path = hyperfile_paths[cuse_hyperfile_index],
    .read.buf = buf,
    .read.size = min(size, sizeof(buf)),
    .read.offset = off,
  });

  assert(nr >= 0);

  fuse_reply_buf(req, buf, nr);
}

static void clop_write(fuse_req_t req, const char *buf, size_t size,
                       off_t off, struct fuse_file_info *fi) {
  (void)fi;

  int nr = hyp_file_op((struct hyperfs_data){
    .type = WRITE,
    .path = hyperfile_paths[cuse_hyperfile_index],
    .write.buf = buf,
    .write.size = size,
    .write.offset = off,
  });

  fuse_reply_write(req, nr);
}

static void clop_ioctl(fuse_req_t req, int cmd, void *arg,
                       struct fuse_file_info *fi, unsigned flags,
                       const void *in_buf, size_t in_bufsz, size_t out_bufsz) {
  (void) arg;
  (void) fi;
  (void) flags;
  (void) in_buf;
  (void) in_bufsz;
  (void) out_bufsz;

  void *ptr = NULL;
  size_t size;
  int rw = -1;
  int result = hyp_file_op((struct hyperfs_data){
    .type = IOCTL,
    .path = hyperfile_paths[cuse_hyperfile_index],
    .ioctl.cmd = cmd,
    .ioctl.ptr = &ptr,
    .ioctl.size = &size,
    .ioctl.rw = &rw,
  });
  if (ptr) {
    struct iovec iov = { ptr, size };
    if (rw == READ) {
      fuse_reply_ioctl_retry(req, NULL, 0, &iov, 1);
    } else {
      assert(rw == WRITE);
      fuse_reply_ioctl_retry(req, &iov, 1, NULL, 0);
    }
  } else {
    fuse_reply_ioctl(req, result, NULL, 0);
  }
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

static void clop_open(fuse_req_t req, struct fuse_file_info *fi) {
  fuse_reply_open(req, fi);
}

static const struct fuse_operations fops = {
    .open = hyperfs_open,
    .getattr = hyperfs_getattr,
    .readdir = hyperfs_readdir,
    .truncate = hyperfs_truncate,

    .readlink = hyperfs_readlink,
    .release = hyperfs_release,

    .init = xmp_init,
    .read = xmp_read,
    .write = xmp_write,
    .ioctl = xmp_ioctl,
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

static const struct cuse_lowlevel_ops clops = {
    .open = clop_open,
    .read = clop_read,
    .write = clop_write,
    .ioctl = clop_ioctl,
};

static void load_hyperfile_paths(void) {
  hc(HYP_GET_NUM_HYPERFILES, (void *[]){&num_hyperfiles}, 1);
  hyperfile_paths = calloc(num_hyperfiles, sizeof(*hyperfile_paths));
  for (size_t i = 0; i < num_hyperfiles; i++) {
    hyperfile_paths[i] = calloc(HYPERFILE_PATH_MAX, 1);
  }
  hc(HYP_GET_HYPERFILE_PATHS, (void **)hyperfile_paths, num_hyperfiles);
}

int main(int argc, char *argv[]) {
  // Parse command-line arguments
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  if (fuse_opt_parse(&args, &options, option_spec, NULL)) {
    exit(EXIT_FAILURE);
  }
  if (!options.passthrough_path) {
    fputs("error: missing --passthrough-path\n", stderr);
    exit(EXIT_FAILURE);
  }

  load_hyperfile_paths();

  // Fork to make a process for each CUSE device, and save their device numbers
  hyperfile_dev_nums = calloc(num_hyperfiles, sizeof(dev_t));
  for (size_t i = 0; i < num_hyperfiles; i++) {
    pid_t pid = fork();
    switch (pid) {
      case -1:
        perror("fork");
        exit(EXIT_FAILURE);
      case 0: {
        cuse_hyperfile_index = i;
        char dev_name[HYPERFILE_PATH_MAX * 2];
        int len = snprintf(
          dev_name,
          sizeof(dev_name),
          "DEVNAME=hyperfs/%s",
          hyperfile_paths[cuse_hyperfile_index]
        );
        if (len >= sizeof(dev_name)) {
          perror("snprintf");
          exit(EXIT_FAILURE);
        }
        struct cuse_info ci;
        memset(&ci, 0, sizeof(ci));
        ci.dev_major = i == 0 ? 0 : hyperfile_dev_nums[0]; // Auto-allocate device number
        ci.dev_minor = i;
        ci.dev_info_argc = 1;
        ci.dev_info_argv = (const char *[]){ dev_name };
        ci.flags = CUSE_UNRESTRICTED_IOCTL;
        exit(cuse_lowlevel_main(args.argc, args.argv, &ci, &clops, NULL));
      }
    }
    waitpid(pid, NULL, 0);
    char dev_path[PATH_MAX];
    int len = snprintf(dev_path, sizeof(dev_path), "/dev/hyperfs/%s", hyperfile_paths[i]);
    if (len >= sizeof(dev_path)) {
      perror("snprintf");
      exit(EXIT_FAILURE);
    }
    struct stat st;
    int err;
    do {
      err = stat(dev_path, &st);
      usleep(10 * 1000); // 0.01 seconds
    } while (err);
    hyperfile_dev_nums[i] = st.st_rdev;
  }
 
  return fuse_main(args.argc, args.argv, &fops, NULL);
}
