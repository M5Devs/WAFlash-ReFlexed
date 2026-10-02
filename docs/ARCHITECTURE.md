# WAFlash Re:Flexed — System Architecture & Core Technical Specification

## 1. Executive Summary & Vision

**WAFlash Re:Flexed** is transitioning from legacy precompiled, closed WebAssembly blobs toward a decoupled, modular, open-source C++ runtime architecture. By combining Adobe's official ActionScript 3 engine (**`adobe-flash/avmplus`**) with the modern vector rendering and SWF parsing pipeline of **`Flare-Animate/Flare`**, Re:Flexed targets cross-platform deployment across WebAssembly (Emscripten), desktop native executables, and a native **Libretro core (`libretro-flash`) with RetroAchievements memory hooking capabilities**.

```
                           +-------------------------------------+
                           |            SWF Container            |
                           +------------------+------------------+
                                              |
                        +---------------------+---------------------+
                        |                                           |
                        v                                           v
         +------------------------------+           +------------------------------+
         |      SWF / ABC Parser        |           |   Timeline & Vector Parser   |
         |         (Flare)              |           |           (Flare)            |
         +--------------+---------------+           +--------------+---------------+
                        |                                           |
                        v                                           v
         +------------------------------+           +------------------------------+
         |   ActionScript 3 Execution   |           |    DisplayList & Vector      |
         |      (AVMPlus Core)          |           |      Rasterizer (Flare)      |
         +--------------+---------------+           +--------------+---------------+
                        |                                           |
                        +---------------------+---------------------+
                                              |
                                              v
                               +-----------------------------+
                               |     Libretro Core Bridge    |
                               |      (src/core_stub.cpp)    |
                               +--------------+--------------+
                                              |
                     +------------------------+------------------------+
                     |                        |                        |
                     v                        v                        v
            +----------------+       +----------------+       +----------------+
            | Libretro Host  |       | WebAssembly    |       | Desktop App    |
            | (RetroArch /   |       | (Emscripten /  |       | (Tauri Native) |
            | RetroAchievs)  |       | Browser Canvas)|       |                |
            +----------------+       +----------------+       +----------------+
```

---

## 2. In-Depth Analysis: `adobe-flash/avmplus` (Scripting Engine)

### 2.1 Virtual Machine & MMgc Architecture
`avmplus` (ActionScript Virtual Machine 2 / AVM2) executes ABC (ActionScript Bytecode) compiled from AS3 source files.

1. **`AvmCore`:** The global state manager for the virtual machine instance. Manages string tables (`StringManager`), class definitions (`ClassClosure`), traits, scopes, and exception handling.
2. **`MMgc` (Memory Manager / Garbage Collector):**
   - **GC Heap Architecture:** Conservative mark-and-sweep combined with generational garbage collection. Memory allocations are partitioned into `GCHeap`, `GCObject`, and fixed-size block pools.
   - **Atoms (`Atom` type):** The fundamental 32-bit (or 64-bit) value representation in AVMPlus.
     - **3-bit Tagging System:**
       - `000` (`kUntagged` / `kObjectType`): Pointer to `ScriptObject` / GC Object.
       - `001` (`kStringPointer`): Pointer to `StringObject`.
       - `010` (`kNamespacePointer`): Pointer to `NamespaceObject`.
       - `011` (`kDoubleType`): Pointer to 64-bit IEEE-754 double floating point value on GC heap.
       - `100` (`kIntegerType`): Immediate 29-bit signed integer (`int` / `uint`). Shifted value stored directly in the Atom without heap allocation.
       - `110` (`kBooleanType` / `kSpecialType`): Represents primitive literals (`null`, `undefined`, `true`, `false`).

```
  Atom Bit Layout (32-bit systems):
  +-------------------------------------------------+-----+
  | Value / Memory Pointer (29 bits)                | Tag |
  +-------------------------------------------------+-----+
  31                                               3     0
```

### 2.2 Memory Inspection & RetroAchievements Integration
RetroAchievements (`rcheevos`) relies on a contiguous, predictable byte array exposed via `RETRO_MEMORY_SYSTEM_RAM`. Because AVMPlus uses GC heap pointer indirections, raw AS3 variables normally drift in memory during GC cycles.

#### Solution: Fixed Symbol Address Register & Reflection Bridge
To expose game variables (HP, Score, Level) consistently to RetroAchievements:

1. **Deterministic Linear Buffer (AS3 State Mirroring):**
   A fixed 1MB–8MB C++ buffer (`SimulatedAVM3MemoryMap` / `g_core.avm_memory.system_ram`) is allocated at launch.
2. **AS3 Memory Binding Native Method:**
   Expose an internal C++ native method `System.writeRetroRam(offset: int, value: int)` to ActionScript 3.
3. **Automated AS3 Memory Mirroring:**
   During the frame tick (`enterFrame`), or whenever key game state properties change, AS3 writes primitive integer state directly into `system_ram` at fixed offsets:
   - `0x0000`: Game ID / SWF Hash
   - `0x0010`: Player Score (uint32_le)
   - `0x0014`: Player Lives (uint32_le)
   - `0x0018`: Player HP / Health (uint32_le)
   - `0x001C`: Current Level / Stage (uint32_le)
   - `0x0020`: Game Flags / Status Bitfield

```
  Virtual Memory Map (Exposed via retro_get_memory_data):
  +----------------------+----------------------------------------------------------+
  | Offset Range         | Description / Variable Mapping                           |
  +----------------------+----------------------------------------------------------+
  | 0x0000 - 0x000F      | SWF Header Hash & Game Identifier Metadata               |
  | 0x0010 - 0x0013      | Active Score (uint32_le)                                 |
  | 0x0014 - 0x0017      | Active Lives Count (uint32_le)                           |
  | 0x0018 - 0x001B      | Player Health / HP (uint32_le)                           |
  | 0x001C - 0x001F      | Stage / Level ID (uint32_le)                             |
  | 0x0020 - 0x003F      | Game State Bitfield & Progress Flags                     |
  | 0x0040 - 0x0FFFFF    | Dynamic Variable Mirroring Scratchpad                    |
  +----------------------+----------------------------------------------------------+
```

### 2.3 Compilability & Emscripten Cross-Compilation
To build `avmplus` with modern Clang/Emscripten:
- **Build Flags:** `-DVM_AVMPLUS -DAVMPLUS_64BIT -DMMGC_64 -fno-rtti -fno-exceptions -O3`
- **JIT vs. Interpreter Mode:** JIT compilation (`nanojit`) must be disabled (`-DAVMPLUS_INTERPRETER`) when building for WebAssembly or embedded Libretro architectures that restrict executable memory (`mprotect`/`W^X`).

---

## 3. In-Depth Analysis: `Flare-Animate/Flare` (Parsing & Rendering)

### 3.1 SWF Parsing & Tag Processing
Flare provides modern SWF tag ingestion and vector representation:
- **Header Parsing:** Uncompresses `FWS` (uncompressed), `CWS` (zlib), and `ZWS` (LZMA) headers.
- **Tag Extraction:**
  - `TagDefineShape1-4`: Parses vector path records (lines, quadratic bezier curves, fills, strokes).
  - `TagDefineBitsJPEG / Lossless`: Extracts embedded raster textures into GPU texture formats.
  - `TagDoABC / TagDoABC2`: Extracts embedded ActionScript 3 bytecode blocks and routes them directly to `AVMPlus`.
  - `TagPlaceObject2/3` & `TagRemoveObject`: Updates frame-by-frame DisplayList placement matrices, color transforms, and blend modes.

### 3.2 Timeline & DisplayList Engine
Flare maintains a hierarchical scene graph representing the active Flash DisplayList:
- **`DisplayObject` -> `InteractiveObject` -> `DisplayObjectContainer` -> `Sprite` -> `MovieClip`**
- **Rasterization Pipeline:**
  - **Vector Processing:** Tessellates path contours into triangle meshes via anti-aliased vector tessellation.
  - **Render Backend:** Supports modular renderers (OpenGL ES 3.0, WebGL 2, or Software Surface Rasterizer for CPU fallback in headless environments).

### 3.3 Decoupling Strategy (AVMPlus + Flare Integration)
Flare handles rendering and scene graph structure, while AVMPlus controls execution logic:

```
    [ Flare SWF Reader ]
         |       |
         |       +-----> ABC Bytecode Tags -----> [ AVMPlus Engine ]
         |                                              |
         v                                              v
   DisplayList Tree <--- DisplayObject Events <---+ Events / Variable Updates
         |
         v
   [ Vector Rasterizer ]
         |
         v
   [ Libretro Video Framebuffer ]
```

---

## 4. Architectural Comparison Matrix

| Feature / Criteria | **Ruffle** (Rust Clean-Room) | **WAFlash Re:Flexed** (AVMPlus + Flare) |
| :--- | :--- | :--- |
| **AS3 Execution Engine** | Custom Rust Interpreter (Incomplete AS3 coverage) | Official Adobe `avmplus` C++ VM (100% Spec Compliant) |
| **Vector Rendering** | `ruffle_render` (wgpu / WebGPU / Canvas2D) | Flare C++ Tessellator (OpenGL ES / Software rasterizer) |
| **Libretro Core Support** | Experimental / Third-party rust wrapper | Native C / C++ Core Header Implementation (`libretro-flash`) |
| **RetroAchievements** | Difficult due to dynamic Rust object allocations | Streamlined via C++ Fixed Memory Bridge (`retro_get_memory_data`) |
| **Web Performance** | High WASM efficiency; high memory usage for complex AS3 | Ultra-low footprint; native C++ performance via Emscripten |
| **Complex AS3 Games** | Fails on obscure AS3 bytecodes & custom reflection | Excellent compatibility (executes original Flash player code) |

---

## 5. Summary & Implementation Alignment

The proposed architecture cleanly decouples bytecode evaluation from rendering while providing a native bridge into the Libretro ecosystem. Developers can target Web browser deployments via Emscripten, native desktop builds via C++/Tauri, and full emulation frontend integration with RetroAchievements support.
