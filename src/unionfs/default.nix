{ unionfs-fuse }:

unionfs-fuse.overrideAttrs (old: {
  patches = old.patches ++ [ ./forward-ioctls.patch ];
  meta.mainProgram = "unionfs";
})
