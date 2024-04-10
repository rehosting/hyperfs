{ nixpkgs, pkgs }:

let
  archs = import ./archs.nix;

  crossBuildPkg = { pkg, arch, env, isStatic }:
    let
      inherit (archs.${arch}) subArch abi gccArch;
      archPkgs = import nixpkgs {
        inherit (pkgs) system;
        crossSystem = {
          config = "${subArch}-linux-${env}${abi}";
          inherit isStatic;
        } // pkgs.lib.optionalAttrs (gccArch != null) { gcc.arch = gccArch; };
      };
    in
    archPkgs.callPackage pkg.override { };

  pkgInfoToLinkFarm = { pkgInfo, arch }:
    let
      normalizedPkgInfo =
        if pkgs.lib.isDerivation pkgInfo then
          {
            pkg = pkgInfo;
            path = "bin/${pkgInfo.meta.mainProgram}";
            env = "musl";
            isStatic = true;
          }
        else
          pkgInfo;
      inherit (normalizedPkgInfo) pkg path env isStatic;
    in
      {
        name = "${baseNameOf path}.${arch}";
        path = "${crossBuildPkg { inherit pkg arch env isStatic; }}/${path}";
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
