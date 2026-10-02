# WAFlash Re:Flexed — Project Development Roadmap

This document outlines the phased transition of WAFlash Re:Flexed from legacy WebAssembly binaries toward a decoupled, open-source C++ core targeting Web, Desktop, and Libretro ecosystem integration.

---

## 🚩 Phase 1: Web Player Stabilization & Architecture Cleanup (Current Phase)
**Goal:** Maintain lightweight standalone web player while laying groundwork for native core integration.

- [x] **Repository Decoupling:** Strip legacy Gatsby / React wrappers (reduced core bundle from 500MB+ to ~15MB).
- [x] **Standalone Offline Support:** Enable local zero-dependency WebAssembly playback with dual SWF drag & drop loading.
- [x] **Architecture Specification:** Draft comprehensive technical documentation (`docs/ARCHITECTURE.md`) detailing `avmplus` execution model, `Flare` rendering pipeline, and `libretro` bridge design.
- [x] **C++ Core Interface Stubs:** Implement `include/libretro.h` and `src/core_stub.cpp` showcasing API callbacks and memory layout bindings.

---

## 🏗️ Phase 2: Standalone AVMPlus & Flare C++ / WASM Engine
**Goal:** Replace proprietary precompiled WASM blobs with an open-source native C++ build pipeline.

- [ ] **AVMPlus Core Compilation:** Cross-compile `adobe-flash/avmplus` using modern Emscripten and Clang toolchains in interpreter mode (`-DAVMPLUS_INTERPRETER`).
- [ ] **Flare SWF Integration:** Pair Flare SWF tag reader and vector path parser with AVMPlus ABC tag loader.
- [ ] **Decoupled Graphics Backend:** Implement a modular hardware-accelerated renderer (OpenGL ES / WebGL2) and CPU software fallback renderer.
- [ ] **WASM Web Build:** Generate clean `waflash-core.wasm` and JS bindings directly from open-source native C++ sources.

---

## 🎮 Phase 3: Libretro Core Implementation & RetroAchievements
**Goal:** Deliver `libretro-flash` shared core library for RetroArch and integrate RetroAchievements memory inspection.

- [ ] **Full Libretro API Implementation:** Complete `src/core_stub.cpp` into a production-ready shared library (`libretro_flash.so` / `.dll` / `.dylib`).
- [ ] **Audio/Video Frame Callbacks:** Synchronize SWF timeline frame rates (e.g. 24fps/30fps/60fps) with Libretro's fixed video refresh rate and audio resampling pipeline.
- [ ] **Input Mapping Engine:** Map Libretro Joypad and Pointer device inputs to ActionScript 3 `MouseEvent` and `KeyboardEvent` instances.
- [ ] **RetroAchievements Memory Mapping:** Establish deterministic AS3 variable mirroring buffer exposed through `retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)` for RAM inspection in `rcheevos`.

---

## 📱 Phase 4: Multiplatform Native Packages
**Goal:** Expand native desktop and mobile app presence across platforms.

- [ ] **Tauri Desktop Application:** Build cross-platform lightweight desktop frontend (Windows, macOS, Linux) utilizing native C++ Flash engine bindings.
- [ ] **Mobile Progressive Web App (PWA):** Enhance web player interface with customizable touch controls, virtual d-pad, and offline service worker caching.
- [ ] **Automated CI/CD Pipeline:** Set up GitHub Actions workflow for cross-compiling Libretro cores across target platform triplets (Linux, Windows, macOS, Android, WebAssembly).
