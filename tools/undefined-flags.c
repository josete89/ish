#include "undefined-flags.h"
#include "ptutil.h"

#define C (1 << 0)
#define P (1 << 2)
#define A (1 << 4)
#define Z (1 << 6)
#define S (1 << 7)
#define O (1 << 11)

int undefined_flags_mask(int pid, struct cpu_state *cpu) {
    addr_t ip = cpu->eip;
    byte_t opcode;
#define read(x) pt_readn(pid, ip++, &x, sizeof(x));
    read(opcode);
    switch (opcode) {
        // shift or rotate, of is undefined if shift count is greater than 1
        case 0x0f:
            read(opcode);
            switch(opcode) {
                case 0xaf: return S|Z|A|P; // imul
                case 0xac: {
                    ip++;
                    byte_t shift_count;
                    read(shift_count);
                    if (shift_count > 0)
                        return A;
                    break;
                }
            }
            break;
        case 0x6b: return S|Z|A|P; // imul

        case 0xc0:
        case 0xc1:
        case 0xd0:
        case 0xd1:
        case 0xd2:
        case 0xd3: {
            ip++; // skip modrm
            byte_t shift_count;
            if (opcode == 0xd0 || opcode == 0xd1)
                shift_count = 1;
            else if (opcode == 0xd2 || opcode == 0xd3)
                shift_count = cpu->cl;
            else
                pt_readn(pid, ip++, &shift_count, sizeof(shift_count));
            if (shift_count > 1)
                return O;
            break;
        }
    }
    return 0;
}
