{
  description = "Havel WM - A modern Wayland compositor built on wlroots";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    wlroots = {
      url = "git+https://gitlab.freedesktop.org/wlroots/wlroots.git?ref=0.20";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, flake-utils, wlroots }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        
        # Havel WM package
        havel-wm = pkgs.stdenv.mkDerivation {
          pname = "havel-wm";
          version = "0.1.0";
          
          src = ./.;
          
          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            wayland-scanner
          ];
          
          buildInputs = with pkgs; [
            wlroots_20
            wayland
            libxkbcommon
            pixman
            dbus
            glib
            nlohmann_json
            libpng
            freetype
            vulkan-loader
            vulkan-headers
            spirv-tools
          ];
          
          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DWLR_USE_UNSTABLE=ON"
          ];
          
          meta = with pkgs.lib; {
            description = "A modern Wayland compositor built on wlroots with plugins and IPC";
            homepage = "https://github.com/havel-wm/havel-wm";
            license = licenses.mit;
            maintainers = [ maintainers.your-name ];
            platforms = platforms.linux;
          };
        };
        
        # Development shell with all dependencies
        devShell = pkgs.mkShell {
          buildInputs = with pkgs; [
            # Build tools
            cmake
            ninja
            pkg-config
            wayland-scanner
            
            # Runtime dependencies
            wlroots_20
            wayland
            libxkbcommon
            pixman
            dbus
            glib
            nlohmann_json
            libpng
            freetype
            vulkan-loader
            vulkan-headers
            spirv-tools
            
            # Testing tools
            wayland-protocols
            
            # Optional runtime dependencies
            waybar
            wofi
            foot
            grim
            slurp
            wlr-randr
            gammastep
            
            # Development tools
            gdb
            valgrind
            lldb
          ];
          
          shellHook = ''
            export WAYLAND_DISPLAY=wayland-1
            export WLR_NO_HARDWARE_CURSORS=1
            export WLR_RENDERER=vulkan
            echo "Havel WM Development Shell"
            echo "=========================="
            echo "Build: cmake -B build -G Ninja && ninja -C build"
            echo "Run: ./build/bin/havel-wm"
            echo ""
            echo "Test with: waybar &"
          '';
        };
      in
      {
        packages = {
          default = havel-wm;
          havel-wm = havel-wm;
        };
        
        apps.default = flake-utils.lib.mkApp {
          drv = havel-wm;
        };
        
        devShells.default = devShell;
        
        # NixOS module
        nixosModules.havel-wm = { config, lib, pkgs, ... }:
          let
            cfg = config.programs.havel-wm;
          in
          {
            options.programs.havel-wm = {
              enable = lib.mkEnableOption "Havel WM compositor";
              
              package = lib.mkOption {
                type = lib.types.package;
                default = havel-wm;
                description = "Havel WM package to use.";
              };
              
              xwayland = lib.mkOption {
                type = lib.types.bool;
                default = true;
                description = "Enable XWayland support.";
              };
              
              extraPackages = lib.mkOption {
                type = lib.types.listOf lib.types.package;
                default = [ ];
                description = "Additional packages to add to PATH.";
              };
            };
            
            config = lib.mkIf cfg.enable {
              nix.environment.systemPackages = [ cfg.package ] ++ cfg.extraPackages;
              
              programs.havel-wm = {
                enable = true;
              };
              
              # Wayland session
              services.xserver.displayManager.sessionPackages = [
                (pkgs.writeTextFile {
                  name = "havel-wm";
                  destination = "/share/wayland-sessions/havel-wm.desktop";
                  text = ''
                    [Desktop Entry]
                    Name=Havel WM
                    Comment=Modern Wayland compositor
                    Exec=${cfg.package}/bin/havel-wm
                    Type=Application
                  '';
                })
              ];
              
              # XWayland support
              services.xserver.xwayland = lib.mkIf cfg.xwayland {
                enable = true;
              };
            };
          };
      }
    );
}
