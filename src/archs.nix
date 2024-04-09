{
  armel = {
    config = "armv7l-linux-gnueabihf";
  };
  mipsel = {
    config = "mipsel-linux-gnu";
    gcc.arch = "mips32r2";
  };
  mipseb = {
    config = "mips-linux-gnu";
    gcc.arch = "mips32r2";
  };
  mips64eb = {
    config = "mips64-linux-gnuabi64";
    gcc.arch = "mips64r2";
  };
}
