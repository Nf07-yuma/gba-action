@ GBA スタートアップ: ROMヘッダ / スタック設定 / RAM初期化 / IRQ ディスパッチ
    .section .crt0, "ax"
    .arm
    .align 2
    .global _start
_start:
    b       rom_start
    .fill   156, 1, 0               @ ロゴ領域 (エミュレータ向け。実機用は tools/fixheader.py --logo で別途)
    .ascii  "STEEL CHAIN "          @ ゲームタイトル (12文字)
    .ascii  "BSCJ"                  @ ゲームコード
    .ascii  "01"                    @ メーカーコード
    .byte   0x96                    @ 固定値
    .byte   0x00                    @ 本体コード
    .byte   0x00                    @ デバイスタイプ
    .fill   7, 1, 0                 @ 予約
    .byte   0x00                    @ バージョン
    .byte   0x00                    @ ヘッダチェックサム (fixheader.py で計算)
    .fill   2, 1, 0                 @ 予約

rom_start:
    mov     r0, #0x12               @ IRQ モード
    msr     cpsr_c, r0
    ldr     sp, =0x03007FA0
    mov     r0, #0x1F               @ System モード
    msr     cpsr_c, r0
    ldr     sp, =0x03007F00

    @ .iwram/.data を ROM からコピー
    ldr     r0, =__iwram_lma
    ldr     r1, =__iwram_start
    ldr     r2, =__iwram_end
1:  cmp     r1, r2
    ldrlt   r3, [r0], #4
    strlt   r3, [r1], #4
    blt     1b

    @ .bss をゼロクリア
    mov     r3, #0
    ldr     r1, =__bss_start
    ldr     r2, =__bss_end
2:  cmp     r1, r2
    strlt   r3, [r1], #4
    blt     2b

    @ .ewram_bss をゼロクリア
    ldr     r1, =__ewram_bss_start
    ldr     r2, =__ewram_bss_end
3:  cmp     r1, r2
    strlt   r3, [r1], #4
    blt     3b

    @ IRQ ハンドラ登録
    ldr     r0, =0x03007FFC
    ldr     r1, =irq_master
    str     r1, [r0]

    ldr     r0, =main
    mov     lr, pc
    bx      r0
    b       .

@ BIOS から ARM モードで呼ばれる。IF を確認し、BIOS 用フラグ(0x03007FF8)へ反映
    .align 2
irq_master:
    mov     r0, #0x04000000
    ldr     r1, [r0, #0x200]        @ 下位16bit=IE, 上位16bit=IF
    and     r1, r1, r1, lsr #16     @ IE & IF
    add     r2, r0, #0x200
    strh    r1, [r2, #2]            @ IF をクリア
    ldr     r2, =0x03007FF8
    ldrh    r3, [r2]
    orr     r3, r3, r1
    strh    r3, [r2]
    @ サウンドミキサ用タイマー割り込み等のフック
    ldr     r2, =irq_user_hook
    ldr     r2, [r2]
    cmp     r2, #0
    bxeq    lr
    stmfd   sp!, {r1, lr}
    mov     r0, r1
    mov     lr, pc
    bx      r2
    ldmfd   sp!, {r1, lr}
    bx      lr

    .pool
