{ pkgs, nixpkgs }:

let
  archs = import ./archs.nix;

  crossBuildPkg = { pkg, arch, env, isStatic }:
    let
      archPkgs = import ./arch-pkgs.nix { inherit pkgs nixpkgs; } archs.${arch} { inherit env isStatic; };
    in
    archPkgs.callPackage pkg.override { };

  pkgInfoToLinkFarm = { pkgInfo, arch }:
    let
      normalizedPkgInfo =
        if pkgs.lib.isDerivation pkgInfo then
          {
            pkg = pkgInfo;
            path = "/bin/${pkgInfo.meta.mainProgram}";
            env = "musl";
            isStatic = true;
          }
        else
          pkgInfo;
      inherit (normalizedPkgInfo) pkg path env isStatic;
    in
      {
        name = "${baseNameOf path}.${arch}";
        path = "${crossBuildPkg { inherit pkg arch env isStatic; }}${path}";
      };

  buildAllArchs =
    pkgInfo:
    let
      pkgArchPairs = pkgs.lib.cartesianProductOfSets {
        inherit pkgInfo;
        arch = builtins.attrNames archs;
      };
    in
    pkgs.linkFarm "build-all-archs" (map pkgInfoToLinkFarm pkgArchPairs);
in
buildAllArchs
