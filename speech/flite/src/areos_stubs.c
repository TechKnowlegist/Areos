/* areos_stubs.c - AREOS addition (not part of Flite).
   Flite can load voices from files or the internet, save .wav files and
   play sound through Linux sound systems. AREOS has none of those yet, and
   plays sound itself (see speech/engine.c), so these all just fail. */
#include "flite.h"
#include "cst_audio.h"
#include "cst_cg.h"

cst_voice *cst_cg_load_voice(const char *voxdir, const cst_lang lang_table[])
{ (void)voxdir; (void)lang_table; return NULL; }
int cst_cg_dump_voice(const cst_voice *v, const cst_string *filename)
{ (void)v; (void)filename; return 0; }
int cst_urlp(const char *url) { (void)url; return 0; }
cst_file cst_url_open(const char *url) { (void)url; return NULL; }
int cst_wave_save_riff(cst_wave *w, const char *filename) { (void)w; (void)filename; return -1; }
int cst_wave_append_riff(cst_wave *w, const char *filename) { (void)w; (void)filename; return -1; }
int play_wave(cst_wave *w) { (void)w; return -1; }
