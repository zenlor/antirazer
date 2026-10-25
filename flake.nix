{
  description = "antirazer — Linux configuration tool for the Razer Naga Left-Handed Edition 2020";

  inputs.nixpkgs.url = "nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }: let
    systems = [ "x86_64-linux" "aarch64-linux" ];
    eachSystem = f:
      nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
  in {
    overlays.default = import ./contrib/openrazer/overlay.nix;

    packages = eachSystem (pkgs: {
      default = import ./nix/package.nix pkgs;
    });

    devShells = eachSystem (pkgs: {
      default = import ./nix/devshell.nix pkgs;
    });

    formatters = eachSystem (pkgs: {
      default = pkgs.nixpkgs-fmt;
    });

    checks = eachSystem (pkgs: {
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
    });
  };
}
