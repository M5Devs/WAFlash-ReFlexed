# ⚡ WAFlash Re:Flexed
> A lightweight, standalone WebAssembly Adobe Flash Player engine with outstanding ActionScript 3 (AS3) compatibility.

## 🌟 Why WAFlash Re:Flexed?
- 🎯 **Superior ActionScript 3 Support:** Runs complex late-era Flash games and tricky rendering tricks (e.g. *Hoshi Saga*, *Learn to Fly*, heavy AS3 physics) where modern clean-room emulators often struggle.
- 🪶 **Ultra-Clean & Lightweight:** Legacy Webpack/Gatsby bundles stripped (reduced from 500MB+ to ~15MB).
- 🔌 **100% Standalone:** Decoupled from external servers — fully offline capable.
- 📂 **Dual File Loading:** Drag & drop any `.swf` file or click **Browse Files** to launch instantly.
- 📺 **Aspect-Ratio Conscious:** Canvas dynamically adapts with Fullscreen support.

## 🚀 How to Run Locally
Run any local static web server in the project directory:
```bash
# Using Python
python3 -m http.server 8000
```
Then open `http://localhost:8000` in your web browser.

## 📜 Credits
- Core WAFlash engine compiled by [vidkidz](https://github.com/vidkidz) using Adobe's AVMPlus & Crossbridge C++ codebase.
- Re-architected and maintained by [M5 Dev](https://github.com/M5Devs).
