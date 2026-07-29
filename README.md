# Zygisk FreeFire IL2CPP Dumper

Multi-fallback IL2CPP dumper with Zygisk injection for Free Fire (`com.dts.freefireth`).

## Output Files

Generated in `/data/data/com.dts.freefireth/files/`:

| File | Description |
|------|-------------|
| `dump.cs` | Full class/method/field definitions with RVA+VA addresses |
| `method_pointers.bin` | Binary array of method RVAs (uint64_t) |
| `il2cpp_info.txt` | Base address and summary info |

## How to use (GitHub Actions)

1. Fork this repo
2. Go to **Actions** tab → **Build Zygisk IL2CPP Dumper** workflow
3. Click **Run workflow**, enter the package name (default: `com.dts.freefireth`)
4. Download the artifact `.zip` when build completes
5. Install in Kitsune Mask / Magisk
6. Restart device → launch game → dumps appear in `/data/data/com.dts.freefireth/files/`

## Build locally

```bash
# Edit package name
sed -i 's/GamePackageName ".*"/GamePackageName "com.dts.freefireth"/' module/src/main/cpp/game.h

# Build
export ANDROID_NDK_HOME=/path/to/ndk
./gradlew :module:assembleRelease
# Output: out/zygisk-ffdumper-*.zip
```

## Fallback methods

The dumper tries these in order:
1. `xdl_open("libil2cpp.so")` — direct in-process resolve
2. `dlopen` with RTLD_NOLOAD — find already-loaded lib
3. Parse `/proc/self/maps` → `dlopen` by full path
4. NativeBridge (LDPlayer x86/x86_64) → load ARM secondary binary via houdini

## Credits

- [Perfare/Zygisk-Il2CppDumper](https://github.com/Perfare/Zygisk-Il2CppDumper) (original)
- [Perfare/Il2CppDumper](https://github.com/Perfare/Il2CppDumper)
- [topjohnwu/Magisk](https://github.com/topjohnwu/Magisk) / [Kitsune Mask](https://github.com/Kitsune-Magisk)
