# OBS Studio Rust, fork by ziyyhhk. Final release.

Status: discontinued. This repository is an unofficial, personal fork of
obs-rust/obs-studio (the independent Rust rewrite of OBS Studio), itself
derived from obsproject/obs-studio. It is not affiliated with the OBS Project,
obsproject.com, or the obs-rust organization. Nobody is maintaining this.

## What this is

A copy of obs-rust/obs-studio with a few small, verified changes by the fork
owner, built for personal use. Roughly half of libobs is Rust, the rest is the
upstream C/C++ hybrid build. It keeps the upstream app identity, settings
folder, plug-in compatibility, and installs beside official OBS Studio.

## What this fork changed (bit exact against the C oracle)

1. SSE2 block kernels for the UYVX packed YUV packers
   (compress_uyvx_to_i420, compress_uyvx_to_nv12, convert_uyvx_to_i444, in
   rust/obs-media-io/src/platform/uyvx.rs). Roughly 2.3x faster on that kernel.
   Helps packed UYVY and YUY2 capture devices. Native NV12 sources are
   unaffected.
2. AVX2 runtime dispatch for the sample_peak volmeter kernel in
   rust/obs-audio-controls, mirroring the upstream true_peak dispatch (#3415).
   Roughly 1 to 3 percent on the audio meter path.
3. Release tooling. A release.yml workflow that builds the Windows portable
   ZIP and BETA MSI on GitHub hosted runners and attaches them to a release.
4. First-launch settings import that no longer aborts when the other app
   holds a file. Browser caches and cookies are skipped as volatile (they
   carry no user settings and can be gigabytes), any other locked file is
   skipped with a warning instead of failing the import, and only the
   global.ini anchor stays mandatory. v0.1.3.

Both kernels are validated with the repo's parity tests, bit for bit against
the unmodified C implementation, including NaN behaviour.

## What optimized means here, and what it does not

1. Recording and streaming CPU is dominated by the encoder (NVENC or x264) and
   the browser source. That code is shared with upstream and is unchanged.
2. The RAM benefit is inherited from upstream, which reported about 19 percent
   lower launch memory. This fork adds no further RAM reduction.
3. The meter and packer changes only matter where they run. They affect audio
   ticks, meters, and packed YUV camera feeds.

## Install (Windows, unsigned)

1. MSI: run OBS-Studio-Rust-0.1.3-windows-x64.msi. It is branded
   OBS-Studio-Rust BETA, and it installs beside official OBS Studio. It is
   unsigned, so Windows SmartScreen warns on first launch. Choose
   More info, then Run anyway. Or right click the file, open Properties,
   tick Unblock, then OK, to silence the prompt.
2. ZIP: unpack anywhere and start bin\64bit\obs64.exe the same way.
3. First launch copies your existing OBS settings, profiles, and scenes into
   %APPDATA%\obs-studio-rust without changing the originals. Upstream changes
   made later are not imported again.

The sound meters are already there: View, then Docks, then Audio Mixer. If the
dock is missing, View then Reset UI restores the default layout.

Other platforms and the signed upstream builds live at
https://github.com/obs-rust/obs-studio/releases. Use those if you want the
upstream project.

## Recording tips

1. Prefer NVENC (Settings, then Output) if you have an NVIDIA GPU. x264 is the
   fallback. Neither is specific to this fork.
2. The app logs into %APPDATA%\obs-studio-rust\basic\logs. Those logs help
   when a capture path misbehaves.

## Credits

Upstream authors, the obs-rust contributors, and the OBS Project whose code is
the base of all of this. This project is not endorsed by them.

GPL-2.0-or-later, like upstream. Derived from
https://github.com/obs-rust/obs-studio (itself a fork of
https://github.com/obsproject/obs-studio). OBS and OBS Studio are trademarks of
their respective owners.
