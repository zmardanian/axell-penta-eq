# Clean-room rules and naming map

Mandatory for this project:

1. No code, GUI graphics, impulse responses or data extracted from commercial plugins.
2. Behaviour is modelled only from **published** circuit descriptions, published specifications
   and measurements, and DSP theory (sources in `reference-eqs.md` and `dsp-literature.md`).
3. Third-party open-source projects were read for *published findings* (e.g. a thesis summary);
   none of their code is used.
4. **No trademarked names** in the UI, mode names, parameter names, preset names, preset
   descriptions or tooltips. Brand names appear only in internal research docs (`docs/research/`)
   only. The README, release notes and all user-facing text use the descriptive names below.

| Studied (internal only) | Name in the plugin |
|---|---|
| Pultec EQP-1A style | **Passive Tube** |
| Neve 1073 style | **Inductor** |
| SSL 4000 E / G style | **Console E/G** (modes **E** and **G**) |
| Maag EQ4 AIR BAND style | **Air** |
| API 550A proportional Q | "proportional Q" (Character mode, Console G) |
| SP-1200 sampler | "dusty sampler" / "12-bit era" in preset names |
| Pro-Q-style workflow | generic terms only (bands, nodes, dynamic, natural phase…) |

A test (`PentaPluginTests`) scans every parameter name, choice label, preset name/description and
tooltip string for a list of protected names and fails the build if one appears.
