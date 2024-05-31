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
  mips64eb = {
    config = "mips64-linux-musl";
    gcc.arch = "mips64r2";
    gcc.abi = "64";
  };
}
