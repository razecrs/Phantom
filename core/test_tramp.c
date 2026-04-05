#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "hook/trampoline.h"

void dummy_hook() {
    printf("Hooked!\n");
}

void original_func() {
    printf("Original\n");
}

int main() {
    printf("Phantom Batch 0: Trampoline Test\n");
    
    trampoline_t t;
    t.target = (void*)original_func;
    t.hook = (void*)dummy_hook;
    t.is_thumb = 0; // ARM64 for now

    uint8_t buf[TRAMP64_SIZE];
    tramp_generate_arm64(buf, t.hook);

    printf("Generated ARM64 trampoline bytes:\n");
    for(int i=0; i<TRAMP64_SIZE; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");

    // Verify LDR X17, #8 ; BR X17 ; .quad addr
    // 51 00 00 58 (LDR X17, #8 in little-endian)
    // 20 02 1F D6 (BR X17 in little-endian)
    if (buf[0] == 0x51 && buf[1] == 0x00 && buf[2] == 0x00 && buf[3] == 0x58 &&
        buf[4] == 0x20 && buf[5] == 0x02 && buf[6] == 0x1F && buf[7] == 0xD6) {
        printf("Verification SUCCESS: Correct ARM64 machine code.\n");
    } else {
        printf("Verification FAILED: Machine code mismatch.\n");
    }

    return 0;
}
