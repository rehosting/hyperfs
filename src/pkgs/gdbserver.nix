pkgs:

pkgs.gdbHostCpuOnly.overrideAttrs (self: {
  # Fix for MIPS+musl
  postPatch =
    self.postPatch or ""
    + ''
      substituteInPlace gdb/mips-linux-nat.c \
        --replace '<sgidefs.h>' '<asm/sgidefs.h>' \
        --replace '_ABIO32' '1'
    '';
  meta.mainProgram = "gdbserver";
  passthru.iglooExcludedArchs = [ "loongarch" ];
})
