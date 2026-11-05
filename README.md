# DAOTransmog

**DAOTransmog** is a native C++20 equipment transmogrification (visual override) mod for ***Dragon Age: Origins*** built on the **[DAOLib](https://github.com/Whocares66-Dev/DAOLib)** native plugin framework.

It allows you to customize your character's visual equipment independently of your actual equipped gear and stats, with full Scaleform GFx in-game UI integration, live 3D paperdoll preview, multi-preset management, and seamless save-game persistence.

---

## Features

- **Integrated Scaleform GFx Wardrobe UI:**
  - A native-styled **`Transmog [T]`** button directly integrated into the top-right header of the inventory screen (`inventory.gfx`) alongside the item sorting switch.
  - Pressing **`T`** or clicking the button smoothly toggles between the standard party inventory list and the Wardrobe Browser overlay.
- **6 Supported Appearance Slots:**
  - **Chest**, **Gloves**, **Boots**, and **Helmet** armor appearances.
  - **Main Hand** and **Offhand Weapon / Shield** appearances independently configured for **Weapon Set I** and **Weapon Set II**.
- **Live Interactive 3D Paperdoll Preview:**
  - Fast in-place preview (`skipFull = 1`) on the inventory 3D paperdoll as you click through appearances, with zero positional shifting or hitching.
  - Dedicated **`[Zoom]`** button to inspect head and helmet details up close.
- **Material Tints & Heraldry Customization:**
  - Cycle through valid material tiers (Iron, Grey Iron, Steel, Veridium, Red Steel, Silverite, Dragonbone).
  - Cycle heraldry emblems for shields and surcoats (Grey Warden Griffon, Cousland, Amell, Redcliffe, Ferelden, etc.).
  - **`[Sync All]`** button to match gloves, boots, and helmet material to your chest piece in a single click.
- **3 Outfits / Presets:**
  - Manage three distinct presets per character: **`[P1]` Travel**, **`[P2]` City**, and **`[P3]` Camp**.
  - Cycle active presets in the world anytime using hotkey **`P`**.
- **Special Slot Actions:**
  - `>> <Use Equipped Item's Real Look>`: Shows your real equipped armor piece or weapon in that slot.
  - `<Hide Slot / Bare Body>`: Hides the slot (bare chest/underwear, bare hands, bare feet, or hidden helmet).
  - `[Apply Outfit]`, `[Reset Slot]`, and `[Discard]` for safe previewing before committing changes to the world.
- **Cross-Slot Consistency:**
  - Automatically handles Mage Robe (`baseItem == 38`) and Massive-No-Boots (`baseItem == 22`) body meshes to prevent clipping or missing hands/feet.
- **Co-Save Persistence & Resume Support:**
  - Saves your transmog configuration directly alongside your game save files into `<SaveSlot>\DAOTransmog.daolib` via DAOLib co-save hooks.
  - Automatically restores your appearance when loading saves or using the Main Menu **Resume** button.
  - Coordinated world refresh (`dao::OnFrame`) applies the saved transmog to the 3D world model the moment exploration begins.

---

## Installation & How to Run

### Prerequisites
1. **Dragon Age: Origins** (version 1.05 / 1.05.13263.0 from Steam, EA App, or GOG).
2. **DAOLib Loader & Addresses:**
   - Install `dinput8.dll` (the DAOLib loader) into your game's executable directory: `<Dragon Age Origins>\bin_ship\`.
   - Install the address library into `<Dragon Age Origins>\bin_ship\daolib\addresses\`.

### Plugin Installation
1. Extract or copy the `DAOTransmog` plugin folder into:
   ```
   <Dragon Age Origins>\bin_ship\daolib\plugins\DAOTransmog\
   ```
2. The folder should contain:
   - `DAOTransmog.dll`
   - `DAOTransmog.pdb` (optional, for debug symbols)
   - `DAOTransmog_en-us.tlk` (English localization)
   - `DAOTransmog_ru-ru.tlk` (Russian localization)
   - `daotransmog_ui.swf`
3. Launch the game. `DAOTransmog` will initialize automatically and create a log file at:
   ```
   Documents\BioWare\Dragon Age\daolib\DAOTransmog.log
   ```

---

## Configuration & Controls

### Hotkeys (Configurable)
| Key | Context | Action |
|:---:|:---|:---|
| **`T`** | Inside Inventory (`I`) | Toggle Transmog Wardrobe Browser / Inventory List |
| **`P`** | In Gameplay / World | Cycle Outfit Preset (Travel $\rightarrow$ City $\rightarrow$ Camp) |
| **`H`** | In Gameplay / World | Toggle Helmet Visibility Mode (Always Show / Hide in Dialogue / Always Hide) |

---

## Building from Source

### Prerequisites
- **Windows 10 / 11**
- **Visual Studio 2022 or 2026** with the **C++ x86 desktop development toolchain** (MSVC 32-bit).
- **CMake** 3.21 or higher.
- **Ninja** build system.
- **Python** 3.10+ (for `.tlk` and ActionScript bytecode compilation scripts).
- **vcpkg** (manifest mode for `nlohmann-json` and `catch2`).
- **DAOLib** source tree (cloned or located alongside `DAO_Transmog`).

### Build Instructions

1. **Clone the repository:**
   ```powershell
   git clone --recurse-submodules https://github.com/Co0lDoge/DAO_Transmog.git
   cd DAO_Transmog
   ```

2. **Ensure DAOLib is present:**
   By default, CMake searches `../DAOLib-master` or `./extern/DAOLib`. You can also set the `DAOLIB_DIR` environment variable:
   ```powershell
   $env:DAOLIB_DIR = "D:\Project\DAOLib-master"
   ```

3. **Initialize the 32-bit MSVC environment:**
   ```cmd
   call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
   ```

4. **Configure with CMake:**
   ```powershell
   cmake --preset release
   ```
   *Or via direct CLI:*
   ```powershell
   cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVCPKG_TARGET_TRIPLET=x86-windows-static-md
   ```

5. **Compile:**
   ```powershell
   cmake --build build/release --config RelWithDebInfo
   ```

6. **Run Unit Tests:**
   ```powershell
   .\build\release\transmog_tests.exe
   ```

---

## CI/CD Workflow

This project includes a **GitHub Actions** CI/CD pipeline defined in [`.github/workflows/ci.yml`](.github/workflows/ci.yml).
- **Triggers:** Every push to `main` and pull requests targeting `main`.
- **Environment:** `windows-latest` with x86 MSVC.
- **Pipeline Steps:**
  1. Checks out the repository and DAOLib with address library dependencies.
  2. Sets up Python and compiles assets.
  3. Configures and builds `DAOTransmog.dll` and test binaries via CMake & Ninja.
  4. Runs all automated unit tests via Catch2 (`transmog_tests.exe`).
  5. Uploads build artifacts (`DAOTransmog.dll`, `.pdb`, `.tlk`, `.swf`) for release deployment.

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
All Dragon Age assets, reverse-engineered addresses, and Scaleform symbols remain property of BioWare and Electronic Arts.
