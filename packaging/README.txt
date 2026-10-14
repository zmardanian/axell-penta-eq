Axell Penta EQ 1.0.0 - Linux x86_64 VST3
=======================================

An all-round EQ by Axell Yobag: 12 analog-matched parametric bands + HP/LP with
dynamic EQ, Mid/Side and side-chain, plus a character rack (Inductor, Console E/G,
Passive Tube, Air), three phase modes (Zero Latency, Natural, Linear), analyzer,
40 loudness-matched factory presets, A/B and undo/redo.

Source code, documentation and issues:
  https://github.com/zmardanian/axell-penta-eq


REQUIREMENTS
------------
- x86_64 Linux with glibc 2.39 or newer (Bazzite, Fedora 40+, Ubuntu 24.04+)
- Any VST3 host. Tested in REAPER.
- System libraries freetype and fontconfig (present on desktop distributions)


INSTALL
-------
In this folder, run:

  ./install.sh

Then restart your DAW and search for "Penta" in its FX browser.

install.sh copies "Axell Penta EQ.vst3" to ~/.vst3. It only replaces an older
copy of Axell Penta EQ, and checks that no system libraries are missing.

Manual install: copy the "Axell Penta EQ.vst3" folder into ~/.vst3 yourself.

If your DAW doesn't find the plugin, add ~/.vst3 to its VST3 path. In REAPER:
Options > Preferences > Plug-ins > VST, add the folder (on Bazzite and other
Fedora atomic systems use the full path /var/home/<your-user>/.vst3), then
click Re-scan.


UNINSTALL
---------
  ./uninstall.sh

or delete ~/.vst3/"Axell Penta EQ.vst3". Your own presets stay in
~/.local/share/AxellPentaEQ/Presets.


LICENCE
-------
Axell Penta EQ is free software under the GNU Affero General Public License v3
(see LICENSE). It is built with JUCE 8.0.15 under JUCE's AGPLv3 option. The
Steinberg VST3 SDK (MIT), the Inter typeface (SIL OFL 1.1) and the other
bundled components are listed in THIRD_PARTY_NOTICES.md.

The complete corresponding source code for this binary is the v1.0.0 tag of:
  https://github.com/zmardanian/axell-penta-eq/tree/v1.0.0

The character modules are behavioural models built from published
specifications and measurements, with descriptive names only; no affiliation
with any equipment manufacturer. This software is based in part on the work of
the Independent JPEG Group. VST is a registered trademark of Steinberg Media
Technologies GmbH.
