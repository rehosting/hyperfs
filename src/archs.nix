{
  x86_64 = {
    config = "x86_64-linux-musl";
  };
  armel = {
    config = "armv7l-linux-musleabi";
  };
  arm64 = {
    config = "aarch64-linux-musl";
  };
  mipsel = {
    config = "mipsel-linux-musl";
    gcc.arch = "mips32r2";
  };
  mipseb = {
    config = "mips-linux-musl";
    gcc.arch = "mips32r2";
  };
  mips64el = {
    config = "mips64el-linux-musl";
    gcc.arch = "mips64r2";
    gcc.abi = "64";
  };
  mips64eb = {
    config = "mips64-linux-musl";
    gcc.arch = "mips64r2";
    gcc.abi = "64";
  };
  ppc = {
    config = "powerpc-linux-musl";
  };
  ppc64 = {
    config = "powerpc64-linux-musl";
    gcc.abi = "elfv2";
  };
  ppc64el = {
    config = "powerpc64le-linux-musl";
  };
  riscv32 = {
    config = "riscv32-linux-musl";
  };
  riscv64 = {
    config = "riscv64-linux-musl";
  };
  loongarch = {
    config = "loongarch64-linux-musl";
    # Remove gcc.arch and gcc.abi settings as they cause build issues
  };
}
