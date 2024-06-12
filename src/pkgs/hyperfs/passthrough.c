/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 * Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>
 * Copyright (C) 2024       Massachusetts Institute of Technology
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

/*
 * This code is derived from libfuse's example/passthrough.c and example/passthrough_helpers.h:
 * https://github.com/libfuse/libfuse/blob/26fa6c1f03f564673f47699eacae45e58fcc0b2d/example/passthrough.c
 * https://raw.githubusercontent.com/libfuse/libfuse/26fa6c1f03f564673f47699eacae45e58fcc0b2d/example/passthrough_helpers.h
 */

static int igloo_rebase_path(const char *src, char *dst) {
  return snprintf(dst, PATH_MAX, "%s%s", options.passthrough_path, src) <
                 PATH_MAX
             ? 0
             : -ENAMETOOLONG;
}

/*
 * Creates files on the underlying file system in response to a FUSE_MKNOD
 * operation
 */
static int mknod_wrapper(int dirfd, const char *path, const char *link,
                         int mode, dev_t rdev) {
  int res;

  if (S_ISREG(mode)) {
    res = openat(dirfd, path, O_CREAT | O_EXCL | O_WRONLY, mode);
    if (res >= 0)
      res = close(res);
  } else if (S_ISDIR(mode)) {
    res = mkdirat(dirfd, path, mode);
  } else if (S_ISLNK(mode) && link != NULL) {
    res = symlinkat(link, dirfd, path);
  } else if (S_ISFIFO(mode)) {
    res = mkfifoat(dirfd, path, mode);
  } else {
    res = mknodat(dirfd, path, mode, rdev);
  }

  return res;
}

static void *xmp_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
  (void)conn;
  cfg->use_ino = 1;

  /* parallel_direct_writes feature depends on direct_io features.
     To make parallel_direct_writes valid, need either set cfg->direct_io
     in current function (recommended in high level API) or set fi->direct_io
     in xmp_create() or xmp_open(). */
  // cfg->direct_io = 1;
  cfg->parallel_direct_writes = 1;

  /* Pick up changes from lower filesystem right away. This is
     also necessary for better hardlink support. When the kernel
     calls the unlink() handler, it does not know the inode of
     the to-be-removed entry and can therefore not invalidate
     the cache of the associated inode - resulting in an
     incorrect st_nlink value being reported for any remaining
     hardlinks to this inode. */
  cfg->entry_timeout = 0;
  cfg->attr_timeout = 0;
  cfg->negative_timeout = 0;

  return NULL;
}

static int xmp_getattr(const char *path, struct stat *stbuf,
                       struct fuse_file_info *fi) {
  (void)fi;
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;
  res = lstat(igloo_path, stbuf);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size) {
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;
  res = readlink(igloo_path, buf, size - 1);
  if (res == -1)
    return -errno;

  buf[res] = '\0';
  return 0;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi,
                       enum fuse_readdir_flags flags) {
  DIR *dp;
  struct dirent *de;
  char igloo_path[PATH_MAX];
  int res;

  (void)offset;
  (void)fi;
  (void)flags;

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;
  dp = opendir(igloo_path);
  if (dp == NULL)
    return -errno;

  while ((de = readdir(dp)) != NULL) {
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_ino = de->d_ino;
    st.st_mode = de->d_type << 12;
    if (filler(buf, de->d_name, &st, 0, 0))
      break;
  }

  closedir(dp);
  return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev) {
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;
  res = mknod_wrapper(AT_FDCWD, igloo_path, NULL, mode, rdev);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_mkdir(const char *path, mode_t mode) {
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;
  res = mkdir(igloo_path, mode);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_unlink(const char *path) {
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;
  res = unlink(igloo_path);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_rmdir(const char *path) {
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;
  res = rmdir(igloo_path);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_symlink(const char *from, const char *to) {
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(to, igloo_path);
  if (res < 0)
    return res;
  res = symlink(from, igloo_path);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_rename(const char *from, const char *to, unsigned int flags) {
  int res;
  char igloo_from[PATH_MAX];
  char igloo_to[PATH_MAX];

  res = igloo_rebase_path(from, igloo_from);
  if (res < 0)
    return res;
  res = igloo_rebase_path(to, igloo_to);
  if (res < 0)
    return res;

  if (flags)
    return -EINVAL;

  res = rename(igloo_from, igloo_to);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_link(const char *from, const char *to) {
  int res;
  char igloo_from[PATH_MAX];
  char igloo_to[PATH_MAX];

  res = igloo_rebase_path(from, igloo_from);
  if (res < 0)
    return res;
  res = igloo_rebase_path(to, igloo_to);
  if (res < 0)
    return res;

  res = link(igloo_from, igloo_to);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
  (void)fi;
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;
  res = chmod(igloo_path, mode);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid,
                     struct fuse_file_info *fi) {
  (void)fi;
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;
  res = lchown(igloo_path, uid, gid);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_truncate(const char *path, off_t size,
                        struct fuse_file_info *fi) {
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;

  if (fi != NULL)
    res = ftruncate(fi->fh, size);
  else
    res = truncate(igloo_path, size);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_create(const char *path, mode_t mode,
                      struct fuse_file_info *fi) {
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;

  res = open(igloo_path, fi->flags, mode);
  if (res == -1)
    return -errno;

  fi->fh = res;
  return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi) {
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;

  res = open(igloo_path, fi->flags);
  if (res == -1)
    return -errno;

  /* Enable direct_io when open has flags O_DIRECT to enjoy the feature
  parallel_direct_writes (i.e., to get a shared lock, not exclusive lock,
  for writes to the same file). */
  if (fi->flags & O_DIRECT) {
    fi->direct_io = 1;
    fi->parallel_direct_writes = 1;
  }

  fi->fh = res;
  return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {
  int fd;
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;

  if (fi == NULL)
    fd = open(igloo_path, O_RDONLY);
  else
    fd = fi->fh;

  if (fd == -1)
    return -errno;

  res = pread(fd, buf, size, offset);
  if (res == -1)
    res = -errno;

  if (fi == NULL)
    close(fd);
  return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi) {
  int fd;
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;

  if (fi == NULL)
    fd = open(igloo_path, O_WRONLY);
  else
    fd = fi->fh;

  if (fd == -1)
    return -errno;

  res = pwrite(fd, buf, size, offset);
  if (res == -1)
    res = -errno;

  if (fi == NULL)
    close(fd);
  return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf) {
  int res;
  char igloo_path[PATH_MAX];

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;

  res = statvfs(igloo_path, stbuf);
  if (res == -1)
    return -errno;

  return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi) {
  (void)path;
  close(fi->fh);
  return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
                     struct fuse_file_info *fi) {
  /* Just a stub.	 This method is optional and can safely be left
     unimplemented */

  (void)path;
  (void)isdatasync;
  (void)fi;
  return 0;
}

static int xmp_ioctl(const char *path, unsigned int cmd, void *arg,
                     struct fuse_file_info *fi, unsigned int flags,
                     void *data) {
  int fd;
  int res;
  char igloo_path[PATH_MAX];

  (void)arg;
  (void)flags;

  res = igloo_rebase_path(path, igloo_path);
  if (res < 0)
    return res;

  if (fi == NULL)
    fd = open(igloo_path, O_RDONLY);
  else
    fd = fi->fh;

  if (fd == -1)
    return -errno;

  res = ioctl(fd, cmd, data);
  if (res == -1)
    res = -errno;

  if (fi == NULL)
    close(fd);
  return res;
}
