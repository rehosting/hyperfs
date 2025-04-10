pkgs:

(pkgs.strace.override { libunwind = null; }).overrideAttrs {
  passthru.iglooExcludedArchs = [ "riscv32" ];
}
