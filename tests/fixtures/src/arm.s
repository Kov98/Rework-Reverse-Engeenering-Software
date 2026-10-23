.syntax unified
.arm
.text
.global entry
.type entry, %function
entry:
    mov r1, #3
    add r0, r0, r1
    bx lr
.size entry, .-entry
