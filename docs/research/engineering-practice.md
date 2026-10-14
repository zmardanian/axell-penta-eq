# Engineering practice — EQ for vocals, boom bap beats, mix bus and master

Collected starting points from published engineering material. Every preset in
`preset-sources.md` cites one or more of the rows below (IDs in brackets).

## Vocals

| ID | Finding | Source |
|---|---|---|
| V1 | High-pass most voices at 80–120 Hz; stay near 80 Hz for rap voices with chest resonance; sweep up until the voice changes, then back off ~10 Hz; 24 dB/oct is common on hip-hop vocals | [MusicGuyMixing — EQ settings for hip hop vocals](https://www.musicguymixing.com/best-eq-settings-for-hip-hop/), [MuseHub — how to EQ vocals](https://www-cf.musehub.com/learn/how-to-eq-vocals) |
| V2 | Mud 200–400 Hz, boxiness ~300–600 Hz: narrow cut, sweep, reduce a few dB — "cutting here often does more for clarity than any boost" | [MuseHub](https://www-cf.musehub.com/learn/how-to-eq-vocals), [Sage Audio — top 10 vocal EQ tips](https://www.sageaudio.com/articles/top-10-vocal-eq-tips), [Rys Up — how to EQ vocals](https://rysupaudio.com/blogs/news/how-to-eq-vocals) |
| V3 | Body of a hip-hop vocal 150–300 Hz; a 1–2 dB lift gives an anchor | [MusicGuyMixing](https://www.musicguymixing.com/best-eq-settings-for-hip-hop/) |
| V4 | Broad 3–5 kHz boost = presence / intelligibility | [MuseHub](https://www-cf.musehub.com/learn/how-to-eq-vocals), [Produce Like A Pro — vocal EQ tips](https://producelikeapro.com/blog/vocal-eq-tips-for-beginning-mixers/) |
| V5 | Sibilance 5–9 kHz (≈3–6 kHz lower voices, 6–8 kHz higher voices); every de-esser is a dynamic EQ — use a dynamic band rather than a static cut | [MuseHub](https://www-cf.musehub.com/learn/how-to-eq-vocals), [Wikipedia — De-essing](https://en.wikipedia.org/wiki/De-essing), [McDSP — de-essing with dynamic EQ](https://mcdsp.com/2021/08/25/blog-2-how-to-use-dynamic-eq-for-vocal-de-essing/) |
| V6 | "Air": a wide shelf from 10 kHz and up (or an ultra-wide shelf at 20–40 kHz) opens the top without harshness | Maag EQ4 manual (see `reference-eqs.md`), [Sage Audio](https://www.sageaudio.com/articles/top-10-vocal-eq-tips) |
| V7 | Telephone / lo-fi: band-limit 300 Hz–3.4 kHz (landline band); more extreme lo-fi HPF 300–600 Hz, LPF 4–7 kHz plus a couple of narrow resonant boosts 600 Hz–4 kHz | [Audiotent — telephone vocal effect](https://audiotent.com/blogs/production-tips/create-telephone-vocal-effect), [Voxbooster](https://voxbooster.com/blog/telephone-voice-effect-online) |
| V8 | Small closet booths: modes/reflections give boxiness around 250–500 Hz; blankets/foam do little below 500 Hz; EQ can only reduce the steady-state colour, not the time-domain smear | [John Sayers archive — VO booth 450 Hz issue](https://johnlsayersarchive.com/viewtopic.php?p=133845), [Gearspace — closet isolation](https://gearspace.com/threads/closet-isolation-question.1378319/), [Neumann — acoustic requirements](https://www.neumann.com/fr-fr/what-are-the-acoustic-requirements-for-recording-and-mixing) |
| V9 | Doubles/backing: thinner low end than the lead (higher HPF), less presence, de-emphasised sibilance so stacks sit behind the lead | [MusicGuyMixing — EQ hip hop vocals](https://www.musicguymixing.com/eq-hip-hop-vocals/), [Produce Like A Pro](https://producelikeapro.com/blog/vocal-eq-tips-for-beginning-mixers/) |
| V10 | Spoken word / podcast: HPF ~80–100 Hz, reduce 200–400 Hz mud, gentle 2–5 kHz presence, de-ess | [Avid — how to EQ vocals](https://www.avid.com/resource-center/how-to-eq-vocals), [MuseHub](https://www-cf.musehub.com/learn/how-to-eq-vocals) |

## Microphones (for the "Axell Signature" presets)

| ID | Finding | Source |
|---|---|---|
| M1 | Shure SM7B: 50 Hz–20 kHz; *bass roll-off* switch ≈ −3 dB @ 200 Hz (below 400 Hz); *presence boost* switch ≈ +3 dB 2–4 kHz. A dynamic mic: darker top, needs presence/air restoring, strong proximity on close rap | [Shure SM7B user guide (McGill copy)](https://www.mcgill.ca/library/files/library/sm7b-user-guide.pdf), [RecordingHacks SM7B](https://recordinghacks.com/microphones/shure/sm7b) |
| M2 | Lewitt LCT 440 Pure: smooth, neutral mids with a slight high-frequency lift ("sweet, airy high end"); not piercing. Needs less air, more care with sibilance | [SOS review](https://www.soundonsound.com/reviews/lewitt-lct-440-pure), [MusicTech review](https://musictech.com/2018/02/lewitt-audio-lct-240-pro-lct-440-pure-review/), [Gearank summary](https://www.gearank.com/node/6454) |

## Beats, drums, instruments

| ID | Finding | Source |
|---|---|---|
| B1 | Kick: weight 50–80 Hz, boxiness ~400 Hz, beater attack 3–5 kHz | [Produce Like A Pro — EQ 14 instruments](https://producelikeapro.com/blog/how-to-eq-14-common-instruments/), [eMastered — EQ drums](https://emastered.com/blog/how-to-eq-drums), [MusicGuyMixing — drum EQ chart](https://www.musicguymixing.com/drum-eq-chart/) |
| B2 | Snare: body ~150–250 Hz, warmth ~400 Hz (or ring to tame), crack/snap 5–10 kHz, harsh ring ~3.5 kHz | [eMastered — snare EQ](https://emastered.com/blog/snare-eq), [Produce Like A Pro](https://producelikeapro.com/blog/how-to-eq-14-common-instruments/) |
| B3 | Hats/cymbals: clang ~200 Hz (high-pass it), sizzle 6–10 kHz; harshness ~2–4 kHz | [Produce Like A Pro](https://producelikeapro.com/blog/how-to-eq-14-common-instruments/), [Dummies — EQ frequency guidelines](https://dummies.com/article/home-recording-eq-frequency-guidelines-179779) |
| B4 | Kick/808 coexistence: give each its own fundamental pocket (e.g. kick ~60 Hz, 808 ~45 Hz); typical kick peak ~64 Hz with an overtone at ~129 Hz; dynamic EQ on the 808 keyed from the kick at the kick's body (~65 Hz) and click (~3.5 kHz) | [Sage Audio — mix kick & 808](https://www.sageaudio.com/articles/how-to-mix-kick-808), [MusicGuyMixing — sidechain EQ](https://www.musicguymixing.com/sidechain-eq/), [MusicGuyMixing — mix 808s](https://www.musicguymixing.com/?p=1602) |
| B5 | 808 translation: 808 audible on small speakers through its 2nd/3rd harmonics (100–250 Hz region) — add harmonics/drive rather than more sub | [Violet Recording — 808s hit harder](https://violetrecording.com/how-to-make-808s-hit-harder/), [MusicGuyMixing — mix 808s](https://www.musicguymixing.com/?p=1602) |
| B6 | SP-1200 character: 26.04 kHz / 12-bit sampling plus the analog SSM2044 low-pass filter → rolled-off, warm, gritty top; the dusty boom-bap sound is band-limited and saturated | [Wikipedia — E-mu SP-1200](https://en.wikipedia.org/wiki/E-mu_SP-1200), [Rossum — SP-1200 education](https://www.rossum-electro.com/pages/education-sp-1200) |
| B7 | Vinyl loops: rumble/sub < 30–40 Hz (turntable, footsteps), surface noise and crackle in the 5–12 kHz band; mono the very low end | [MasteringTheMix — mid/side EQ](https://www.masteringthemix.com/blogs/learn/how-to-use-mid-side-eq-to-elevate-your-masters), general vinyl-transfer practice |
| B8 | Keys/Rhodes: clean below ~80–100 Hz, mud 200–300 Hz, bell/tine 1–3 kHz; guitar: HPF 80–100, mud 200–300, presence 2–5 kHz | [Produce Like A Pro — EQ 14 instruments](https://producelikeapro.com/blog/how-to-eq-14-common-instruments/) |

## Mix bus and mastering

| ID | Finding | Source |
|---|---|---|
| X1 | Mastering moves are usually < 2 dB, wide (Q 0.5–1), "invisible"; clean boxiness, tame harshness, add air | [MasteringBox — mastering EQ guide](https://next.masteringbox.com/learn/mastering-eq-guide), [Violet — best mastering EQs](https://violetrecording.com/best-mastering-eq-plugins/) |
| X2 | Mono bass: high-pass the **side** channel below 100–120 Hz; gentle +0.5–1 dB low shelf under 100 Hz with wide Q for weight | [Sonarworks — M/S mastering tips](https://www.sonarworks.com/blog/learn/pro-mastering-tips-mid-side-eq), [MasteringTheMix](https://www.masteringthemix.com/blogs/learn/how-to-use-mid-side-eq-to-elevate-your-masters) |
| X3 | Width: side-only low cut ~40–50 Hz (12 dB/oct) plus a ≤1 dB, Q 1 boost 2–7 kHz in the side | [MasteringTheMix](https://www.masteringthemix.com/blogs/learn/how-to-use-mid-side-eq-to-elevate-your-masters) |
| X4 | Linear phase keeps L/R (and M/S) phase relationships intact for mastering moves; pre-ringing grows with resolution and steepness — use it as a problem solver | [MasteringTheMix](https://www.masteringthemix.com/blogs/learn/how-to-use-mid-side-eq-to-elevate-your-masters), [FabFilter help](https://www.fabfilter.com/help/pro-q/using/processingmode) |
| X5 | Passive-tube LF trick on the bus: low boost + atten at 30–60 Hz gives weight with a dip above that clears mud — classic for boom bap warmth | SOS (Robjohns 2010), [Puremix — low-end trick](https://www.puremix.com/blog/fab-dupont-pultec-low-end-trick) |
| X6 | Dynamic low-end control on the master: dynamic band at 40–80 Hz with small range (1–3 dB) catches boomy notes only | [Pro-Q help / dynamic EQ](https://www.fabfilter.com/help/pro-q/using/processingmode), McDSP dynamic EQ article |
| X7 | Loudness prep: remove sub-sonic energy (< 25–30 Hz) that eats limiter headroom; tame 2–5 kHz harshness that gets exaggerated by limiting | [MasteringBox](https://next.masteringbox.com/learn/mastering-eq-guide), [Sage Audio](https://www.sageaudio.com/articles/top-10-vocal-eq-tips) |
