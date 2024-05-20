{
  x86_64 = {
    isStatic = true;
    config = "x86_64-linux-musl";
  };
  armel = {
    isStatic = true;
    config = "armv7l-linux-musleabi";
  };
  arm64 = {
    isStatic = true;
    config = "aarch64-linux-musl";
  };
  mipsel = {
    isStatic = true;
    config = "mipsel-linux-musl";
    gcc.arch = "mips32r2";
  };
  mipseb = {
    isStatic = true;
    config = "mips-linux-musl";
    gcc.arch = "mips32r2";
  };
  mips64eb = {
    isStatic = true;
    config = "mips64-linux-musl";
    gcc.arch = "mips64r2";
    gcc.abi = "64";
  };
}
