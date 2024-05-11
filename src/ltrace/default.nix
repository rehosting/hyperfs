{ stdenv, fetchFromGitHub, fetchgit, autoreconfHook, libelf }:

stdenv.mkDerivation {
  name = "igloo-ltrace";
  src = fetchFromGitHub {
    owner = "sparkleholic";
    repo = "ltrace";
    rev = "c22d359433b333937ee3d803450dc41998115685";
    sha256 = "sha256-FOS6CUIR+C86m5xS+OnRjrVDszQuZb6iamc8UTCKrkk=";
  };
  nativeBuildInputs = [ autoreconfHook ];
  buildInputs = [ libelf ];
  prePatch = let
    meta-openembedded = fetchgit {
      url = "https://git.openembedded.org/meta-openembedded";
      rev = "01d3dca6e991e7da7ea9f181a75536a430e1bece";
      sha256 = "sha256-EzTKswrCsg4cp0Jd0fRRjqjBZeX4/0YMey2PKk5U7ps=";
    };
  in ''
    patches="${meta-openembedded}/meta-oe/recipes-devtools/ltrace/ltrace/*.patch $patches"
    mkdir -p config/{autoconf,m4}
  '';
  meta.mainProgram = "ltrace";
}
