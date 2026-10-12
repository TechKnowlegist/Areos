/* fonts.S - the TrueType fonts, built into the program as data. */
	.section .rodata
	.balign 16
	.globl wfont_data_table
wfont_data_table:
	.quad f_sans_r, f_sans_r_end - f_sans_r
	.quad f_sans_b, f_sans_b_end - f_sans_b
	.quad f_sans_i, f_sans_i_end - f_sans_i
	.quad f_sans_bi, f_sans_bi_end - f_sans_bi
	.quad f_serif_r, f_serif_r_end - f_serif_r
	.quad f_serif_b, f_serif_b_end - f_serif_b
	.quad f_serif_i, f_serif_i_end - f_serif_i
	.quad f_mono_r, f_mono_r_end - f_mono_r
	.quad f_mono_b, f_mono_b_end - f_mono_b
	.quad f_sym_r, f_sym_r_end - f_sym_r
	.quad f_sym_b, f_sym_b_end - f_sym_b

	.balign 16
f_sans_r:	.incbin "desktop/fonts/LiberationSans-Regular.ttf"
f_sans_r_end:
	.balign 16
f_sans_b:	.incbin "desktop/fonts/LiberationSans-Bold.ttf"
f_sans_b_end:
	.balign 16
f_sans_i:	.incbin "desktop/fonts/LiberationSans-Italic.ttf"
f_sans_i_end:
	.balign 16
f_sans_bi:	.incbin "desktop/fonts/LiberationSans-BoldItalic.ttf"
f_sans_bi_end:
	.balign 16
f_serif_r:	.incbin "desktop/fonts/LiberationSerif-Regular.ttf"
f_serif_r_end:
	.balign 16
f_serif_b:	.incbin "desktop/fonts/LiberationSerif-Bold.ttf"
f_serif_b_end:
	.balign 16
f_serif_i:	.incbin "desktop/fonts/LiberationSerif-Italic.ttf"
f_serif_i_end:
	.balign 16
f_mono_r:	.incbin "desktop/fonts/LiberationMono-Regular.ttf"
f_mono_r_end:
	.balign 16
f_mono_b:	.incbin "desktop/fonts/LiberationMono-Bold.ttf"
f_mono_b_end:
	.balign 16
f_sym_r:	.incbin "desktop/fonts/DejaVuSans.ttf"
f_sym_r_end:
	.balign 16
f_sym_b:	.incbin "desktop/fonts/DejaVuSans-Bold.ttf"
f_sym_b_end:
	.section .note.GNU-stack,"",@progbits
