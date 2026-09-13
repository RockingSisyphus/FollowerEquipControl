# Follower Equip Control

Follower Equip Control is a native SKSE plugin for Skyrim Special Edition and Anniversary Edition that extends the trade menu with follower-management features, allowing item actions normally available to the player to be applied directly to followers.

This repository is intended for source distribution and development. User-facing installation instructions and other resources are available on the Nexus Mods page:

https://www.nexusmods.com/skyrimspecialedition/mods/175124

## Requirements

### Runtime

* Skyrim Special Edition (1.5.97) or Anniversary Edition (1.6.x)
* SKSE64
* Address Library for SKSE Plugins
* SkyUI
* SKSE Menu Framework 3
  * Optional. Required for the in-game Mod Control Panel.

Skyrim VR is not supported.

### Build

* Visual Studio or Build Tools with the C++ workload
* CMake 3.21 or newer
* Ninja
* Git
* vcpkg
* CommonLibSSE-NG

## Getting the source

Clone the repository with submodules:

```bat
git clone --recurse-submodules <repository-url>
cd <repository-folder>
```

If the repository was cloned without submodules, initialize them with:

```bat
git submodule update --init --recursive
```

The expected CommonLibSSE-NG submodule path is:

```text
extern/CommonLibSSE-NG
```

An external CommonLibSSE-NG checkout can also be used by setting `COMMONLIBSSE_NG_PATH`.

## vcpkg

The project uses vcpkg manifest mode. Set `VCPKG_ROOT` to your vcpkg installation directory:

```bat
setx VCPKG_ROOT "C:\path\to\vcpkg"
```

Open a new terminal after setting the variable.

Dependencies are listed in `vcpkg.json`.

## Building

The default preset builds the Skyrim SE/AE target:

```bat
BuildRelease.bat
```

This configures and builds using:

```bat
cmake -S . --preset Skyrim
cmake --build build --config Release
```

A different configuration can be selected with the second argument:

```bat
BuildRelease.bat Skyrim Debug
BuildRelease.bat Skyrim RelWithDebInfo
```

## Optional deployment

The build can copy the plugin DLL to a local Skyrim or Mod Organizer 2 mod folder after a successful build.

To deploy directly to a Skyrim `Data` folder:

```bat
setx SKYRIM_FOLDER "C:\path\to\Skyrim Special Edition"
```

To deploy to a Mod Organizer 2 `mods` folder:

```bat
setx SKYRIM_MODS_FOLDER "C:\path\to\ModOrganizer\mods"
```

When `SKYRIM_MODS_FOLDER` is set, the plugin is deployed to:

```text
<SKYRIM_MODS_FOLDER>/Follower Equip Control SKSE/SKSE/Plugins
```

These variables are optional. If neither is set, the project still builds normally.

## License

Follower Equip Control is released under the GNU General Public License v3.0. See `LICENSE` for details.

Includes the SKSE Menu Framework header (LGPL-2.1): https://github.com/QTR-Modding/SKSE-Menu-Framework-3
