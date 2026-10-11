# Flite, inside AREOS

This folder is a copy of part of **Flite** ("festival-lite"), the small
speech synthesizer from Carnegie Mellon University: https://github.com/festvox/flite

AREOS uses it as the voice of its built-in screen reader. Flite's licence
is in [COPYING](COPYING); it lets anyone use and share it, as long as the
copyright notices stay, and changes are marked.

## What's here

Only what AREOS needs: Flite's core (`src/`), US English (`lang/usenglish`),
the CMU pronouncing dictionary (`lang/cmulex`) and the "slt" voice
(`lang/cmu_us_slt`, a US English female voice).

## What AREOS changed

- `src/audio/au_streaming.c`: kept the streaming support, removed the example
  sound player (`audio_stream_chunk`). AREOS plays sound through its own driver.
- `src/areos_stubs.c` (new): loading voices from files or the internet,
  saving `.wav` files and Linux sound output all just fail, as AREOS has
  none of those yet.
- `src/cg/cst_spamf0.c`: one line marked "AREOS": the voice's pitch is multiplied by
  Flite's `f0_shift` setting, so the Rob voice can be made higher or deeper too.
- `lang/cmulex`: four data files that are `#include`d by others were renamed
  from `.c` to `.inc`, so the build doesn't compile them on their own.
- Left out: the audio drivers, sockets, `cst_args`, `cst_url`, `cst_ssml`, the
  file-loading voice code (`cst_cg_load_voice`, `cst_cg_dump_voice`) and the
  other voices and languages.

Everything else is exactly as Flite ships it.
