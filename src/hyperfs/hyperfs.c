#define _GNU_SOURCE
#define FUSE_USE_VERSION 35
#define MAGIC_VALUE 0x51ec3692 // crc32("hyperfs")
#define PACKED __attribute__((packed))

#include <errno.h>
#include <fuse.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>

#include "hypercall.h"

enum { READ, WRITE, IOCTL };

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
  } PACKED;
} PACKED;

static const char *hyperfile_paths;

static const struct fuse_opt fuse_opts[] = {
    {"--hyperfile-paths=%s", 0, 1},
};

static void for_each_path(void (*func)(const char *path,
                                       const char *target_path, void *data),
                          const char *target_path, void *data) {
  char *hyperfile_paths_mut = strdup(hyperfile_paths);
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

static int succeed_if_exists(const char *path) {
  return exists(path) ? 0 : -ENOENT;
}

static int hyperfs_open(const char *path, struct fuse_file_info *fi) {
  fi->direct_io = 1;
  return succeed_if_exists(path);
}

static int hyperfs_getattr(const char *path, struct stat *st,
                           struct fuse_file_info *fi) {
  (void)fi;
  int mode = lookup_mode(path);

  if (mode >= 0) {
    memset(st, 0, sizeof(struct stat));
    st->st_nlink = !strcmp(path, "/") ? 2 : 1;
    st->st_mode = mode;
    return 0;
  } else {
    return -ENOENT;
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
    filler(buf, file_name, NULL, 0, 0);
    free(file_name);
  }
}

static int hyperfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi,
                           enum fuse_readdir_flags flags) {
  (void)offset;
  (void)fi;
  (void)flags;

  if (lookup_mode(path) != DIR_MODE) {
    return -ENOENT;
  }

  filler(buf, ".", NULL, 0, 0);
  filler(buf, "..", NULL, 0, 0);

  void *ptrs[] = {filler, buf};
  for_each_path(readdir_func, path, ptrs);

  return 0;
}

static int hyperfs_truncate(const char *path, off_t offset,
                            struct fuse_file_info *fi) {
  (void)offset;
  (void)fi;
  return succeed_if_exists(path);
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

static int hyperfs_read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
  (void)fi;

  struct hyperfs_data data = {.type = READ,
                              .path = path,
                              .read.buf = buf,
                              .read.size = size,
                              .read.offset = offset};
  void *s[] = {&data};
  return lookup_mode(path) == DEV_MODE ? page_in_hyperfs_data(&data),
         hc(MAGIC_VALUE, s, 1)         : -ENOENT;
}

static int hyperfs_write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {
  (void)fi;

  struct hyperfs_data data = {.type = WRITE,
                              .path = path,
                              .write.buf = buf,
                              .write.size = size,
                              .write.offset = offset};
  void *s[] = {&data};
  return lookup_mode(path) == DEV_MODE ? page_in_hyperfs_data(&data),
         hc(MAGIC_VALUE, s, 1)         : -ENOENT;
}

static int hyperfs_ioctl(const char *path, unsigned int cmd, void *arg,
                         struct fuse_file_info *fi, unsigned int flags,
                         void *data_) {
  (void)arg;
  (void)fi;
  (void)flags;

  struct hyperfs_data data = {
      .type = IOCTL, .path = path, .ioctl.cmd = cmd, .ioctl.data = data_};
  void *s[] = {&data};
  return lookup_mode(path) == DEV_MODE ? page_in_hyperfs_data(&data),
         hc(MAGIC_VALUE, s, 1)         : -ENOENT;
}

static const struct fuse_operations fops = {
    .open = hyperfs_open,
    .getattr = hyperfs_getattr,
    .readdir = hyperfs_readdir,
    .truncate = hyperfs_truncate,
    .read = hyperfs_read,
    .write = hyperfs_write,
    .ioctl = hyperfs_ioctl,
};

int main(int argc, char *argv[]) {
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  if (fuse_opt_parse(&args, &hyperfile_paths, fuse_opts, NULL)) {
    return 1;
  }
  if (!hyperfile_paths) {
    fputs("error: missing --hyperfile-paths\n", stderr);
  }
  return fuse_main(args.argc, args.argv, &fops, NULL);
}
