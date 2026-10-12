/* jsprelude.S - the JavaScript that builds window, document and the other web
   APIs (the prelude folder, joined into bin/js/prelude.js) is part of the browser. */
        .section .rodata
        .globl jsh_prelude_start
jsh_prelude_start:
        .incbin "bin/js/prelude.js"
        .byte 0
        .section .note.GNU-stack,"",@progbits
