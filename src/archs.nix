{
  armel = {
    subArch = "arm";
    abi = "eabi";
    gccArch = null;
    uClibcSupport = false;
  };
  # mipsel = { subArch = "mipsel"; abi = ""; gccArch = "mips32r2"; uClibcSupport = true; };
  # mipseb = { subArch = "mips"; abi = ""; gccArch = "mips32r2"; uClibcSupport = false; };
  mips64eb = { subArch = "mips64"; abi = "abi64"; gccArch = "mips64r2"; uClibcSupport = false; };
}
