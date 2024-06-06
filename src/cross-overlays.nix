# List of overlays to be applied for cross-compiled packages
[

  # Disable libfuse's /etc/mtab handling with util-linux's mount command
  (self: super: {
    fuse3 = super.fuse3.overrideAttrs
      (o: { mesonFlags = o.mesonFlags ++ [ "-Ddisable-mtab=true" ]; });
  })

  # Remove now-unneeded util-linux dependency to speed up build
  (self: super: {
    fuse3 = super.fuse3.override { util-linux = super.emptyDirectory; };
  })

  # The p11-kit tests seem to fail for single-user Nix installs
  (self: super: {
    p11-kit = super.p11-kit.overrideAttrs { doCheck = false; };
  })

]
