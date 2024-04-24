{ pkgs, nixpkgs }:

{ subArch, abi, gccArch, uClibcSupport }:

{ env, isStatic }:

import nixpkgs {
  inherit (pkgs) system;
  crossSystem = {
    config = "${subArch}-linux-${if env == "uclibc" -> uClibcSupport then env else "musl"}${abi}";
    inherit isStatic;
  } // pkgs.lib.optionalAttrs (gccArch != null) { gcc.arch = gccArch; };
}
