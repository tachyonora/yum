{
  description = "yum, a simple text editor with basic highlight for C";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "yum";
          version = "1.0";

          src = self;

          nativeBuildInputs = [ ];
          buildInputs = [ ];

          buildPhase = ''
            runHook preBuild
            make
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp yum $out/bin/
            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "a simple text editor with basic highlight for C";
            mainProgram = "yum";
            platforms = platforms.linux ++ platforms.darwin;
          };
        };

        apps.default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/yum";
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
          packages = with pkgs; [
            gdb
            gnumake
          ];
        };
      }
    );
}
