{ stdenv, fetchFromGitHub, autoreconfHook, libelf }:

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
    meta-openembedded = fetchFromGitHub {
      owner = "openembedded";
      repo = "meta-openembedded";
      rev = "f804417cda245e073c38fbdd6749e0bd49a1c84d";
      sha256 = "sha256-br775iAAWTKDlteU9SmGV7xe+NLV0lc/xgdc/XOlBjc=";
    };
  in ''
    patches="${meta-openembedded}/meta-oe/recipes-devtools/ltrace/ltrace/*.patch $patches"
    mkdir -p config/{autoconf,m4}
  '';
  meta.mainProgram = "ltrace";
}
