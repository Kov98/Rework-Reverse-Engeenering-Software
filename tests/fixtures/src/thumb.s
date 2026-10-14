.syntax unified
.thumb
.text
.global entry
.thumb_func
.type entry, %function
entry:
    movs r1, #3
    adds r0, r0, r1
    bx lr
.size entry, .-entry
