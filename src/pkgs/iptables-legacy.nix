pkgs:

# In modern nixpkgs, the plain "iptables" symlink points to the NFT backend,
# so we must explicitly request "iptables-legacy" (→ xtables-legacy-multi).
pkgs.iptables // {
  iglooName = "iptables-legacy";
  meta = pkgs.iptables.meta // { mainProgram = "iptables-legacy"; };
}
