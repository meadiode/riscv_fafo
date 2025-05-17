#version 430 core

#define PERIPH_START 0x01000000
#define VRAM_START   0x01000028
#define ROM_START    0x08000000
#define RAM_START    0x20000000

#define DISP_WIDTH     320
#define DISP_HEIGHT    200
#define DISP_VRAM_SIZE (DISP_WIDTH * DISP_HEIGHT)

#define ROM_SIZE (1024 * 1024 * 16 / 4)
#define RAM_SIZE (1024 * 1024 * 8 / 4)
#define PERIPH_SIZE 10

layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout (rgba8, binding = 0) uniform image2D displays;

struct cpu_t
{
    uint regs[32];
    uint pc;
    uint exit_addr;
    uint periph[16];
};

struct uinst_t
{
    uint inst_id;
    uint rd;
    uint rs1;
    uint rs2;
    int  imm;
};


#define INST_NOP           0
#define INST_ADD           1
#define INST_SUB           2
#define INST_MUL           3
#define INST_XOR           4
#define INST_DIV           5
#define INST_OR            6
#define INST_REM           7
#define INST_AND           8
#define INST_REMU          9
#define INST_CZERO_NEZ     10
#define INST_SLL           11
#define INST_MULH          12
#define INST_SRL           13
#define INST_SRA           14
#define INST_DIVU          15
#define INST_CZERO_EQZ     16
#define INST_SLT           17
#define INST_MULHSU        18
#define INST_SLTU          19
#define INST_MULHU         20
#define INST_ADDI          21
#define INST_XORI          22
#define INST_ORI           23
#define INST_ANDI          24
#define INST_SLLI          25
#define INST_SRLI          26
#define INST_SRAI          27
#define INST_SLTI          28
#define INST_SLTIU         29
#define INST_SB            30
#define INST_SH            31
#define INST_SW            32
#define INST_LB            33
#define INST_LH            34
#define INST_LW            35
#define INST_LBU           36
#define INST_LHU           37
#define INST_BEQ           38
#define INST_BNE           39
#define INST_BLT           40
#define INST_BGE           41
#define INST_BLTU          42
#define INST_BGEU          43
#define INST_JAL           44
#define INST_JALR          45
#define INST_LUI           46
#define INST_AUIPC         47
#define INST_ECALL         48
#define INST_BREAK         49
#define INST_INVALID       50
#define NUM_INSTS          51


layout (std430, binding = 1) buffer rv_cpus_layout
{
    cpu_t cpus[];
};

layout (std430, binding = 2) readonly buffer rv_rom_layout
{
    uint rom[];
};

layout (std430, binding = 3) buffer rv_ram_layout
{
    uint ram[];
};

layout (std430, binding = 4) readonly buffer rv_uinsts_layout
{
    uinst_t uinsts[];
};

uniform uint n_cycles;
uniform uint time_ms;

float rand(vec2 co)
{ 
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

bool device_write_byte(in uint dev_id, in uint addr, in uint data)
{
    if (addr >= RAM_START)
    {
        uint offset = addr - RAM_START;
        uint word_id = offset >> 2;
        
        if (word_id >= RAM_SIZE)
        {
            return false;
        }

        uint word = ram[dev_id * RAM_SIZE + word_id] & ~(0xff << ((offset & 0x03) << 3));
        ram[dev_id * RAM_SIZE + word_id] = word | ((data & 0xff) << ((offset & 0x03) << 3));
        return true;
    }
    else if (addr >= VRAM_START)
    {
        uint offset = addr - VRAM_START;
        uint word_id = offset >> 2;

        if (word_id >= DISP_VRAM_SIZE)
        {
            return false;
        }

        ivec2 origin = ivec2(gl_GlobalInvocationID.x * DISP_WIDTH,
                             gl_GlobalInvocationID.y * DISP_HEIGHT);
        ivec2 coord = origin + ivec2(word_id % DISP_WIDTH, word_id / DISP_WIDTH);
        vec4 pixel = imageLoad(displays, coord);
        pixel[offset & 0x03] = float(data) / 255.0;
        imageStore(displays, coord, pixel);
        return true;
    }
    else if (addr >= PERIPH_START)
    {
        uint offset = addr - PERIPH_START;
        uint word_id = offset >> 2;
        
        if (word_id >= PERIPH_SIZE)
        {
            return false;
        }

        uint word = cpus[dev_id].periph[word_id] & ~(0xff << ((offset & 0x03) << 3));
        cpus[dev_id].periph[word_id] = word | ((data & 0xff) << ((offset & 0x03) << 3));
        return true;
    }

    return false;
}


bool device_write_hword(in uint dev_id, in uint addr, in uint data)
{
    bool res = true;
    res = res && device_write_byte(dev_id, addr + 0, data & 0xff);
    res = res && device_write_byte(dev_id, addr + 1, (data >> 8) & 0xff);

    return res;
}


bool device_write_word(in uint dev_id, in uint addr, in uint data)
{
    bool res = true;

    if (0 == (addr & 0x03))
    {
        if (addr >= RAM_START)
        {
            uint word_id = (addr - RAM_START) >> 2;

            if (word_id >= RAM_SIZE)
            {
                return false;
            }

            ram[dev_id * RAM_SIZE + word_id] = data;
        }
        else if (addr >= VRAM_START)
        {
            uint word_id = (addr - VRAM_START) >> 2;

            if (word_id >= DISP_VRAM_SIZE)
            {
                return false;
            }

            ivec2 origin = ivec2(gl_GlobalInvocationID.x * DISP_WIDTH,
                                 gl_GlobalInvocationID.y * DISP_HEIGHT);
            ivec2 coord = origin + ivec2(word_id % DISP_WIDTH, word_id / DISP_WIDTH);
            vec4 pixel = vec4((data & 0xff),
                              ((data >> 8) & 0xff),
                              ((data >> 16) & 0xff),
                              ((data >> 24) & 0xff)) / 255.0;
            imageStore(displays, coord, pixel);
        }
        else if (addr >= PERIPH_START)
        {
            uint word_id = (addr - PERIPH_START) >> 2;

            if (word_id >= PERIPH_SIZE)
            {
                return false;
            }

            cpus[dev_id].periph[word_id] = data;
        }
    }
    else
    {
        res = res && device_write_byte(dev_id, addr + 0, data & 0xff);
        res = res && device_write_byte(dev_id, addr + 1, (data >> 8) & 0xff);
        res = res && device_write_byte(dev_id, addr + 2, (data >> 16) & 0xff);
        res = res && device_write_byte(dev_id, addr + 3, (data >> 24) & 0xff);
    }

    return res;
}


bool device_read_byte(in uint dev_id, in uint addr, out uint data)
{
    if (addr >= RAM_START)
    {
        uint offset = addr - RAM_START;
        uint word_id = offset >> 2;
        
        if (word_id >= RAM_SIZE)
        {
            return false;
        }

        data = (ram[dev_id * RAM_SIZE + word_id] >> ((offset & 0x03) << 3)) & 0xff;
        return true;
    }
    else if (addr >= ROM_START)
    {
        uint offset = addr - ROM_START;
        uint word_id = offset >> 2;
        
        if (word_id >= ROM_SIZE)
        {
            return false;
        }

        data = (rom[word_id] >> ((offset & 0x03) << 3)) & 0xff;
        return true;
    }
    else if (addr >= PERIPH_START)
    {
        uint offset = addr - PERIPH_START;
        uint word_id = offset >> 2;
        
        if (word_id >= PERIPH_SIZE)
        {
            return false;
        }

        data = (cpus[dev_id].periph[word_id] >> ((offset & 0x03) << 3)) & 0xff;
        return true;
    }

    return false;
}


bool device_read_hword(in uint dev_id, in uint addr, out uint data)
{
    bool res = true;
    uint val = 0;

    res = res && device_read_byte(dev_id, addr, val);
    data = val;
    res = res && device_read_byte(dev_id, addr + 1, val);
    data |= (val << 8);

    return res;
}


bool device_read_word(in uint dev_id, in uint addr, out uint data)
{
    bool res = true;

    if (0 == (addr & 0x03))
    {
        if (addr >= RAM_START)
        {
            uint offset = addr - RAM_START;
            uint word_id = offset >> 2;
            
            if (word_id >= RAM_SIZE)
            {
                return false;
            }

            data = ram[dev_id * RAM_SIZE + word_id];
        }
        else if (addr >= ROM_START)
        {
            uint offset = addr - ROM_START;
            uint word_id = offset >> 2;
            
            if (word_id >= ROM_SIZE)
            {
                return false;
            }

            data = rom[word_id];
        }
        else if (addr >= PERIPH_START)
        {
            uint offset = addr - PERIPH_START;
            uint word_id = offset >> 2;
            
            if (word_id >= PERIPH_SIZE)
            {
                return false;
            }

            data = cpus[dev_id].periph[word_id];
        }
    }
    else
    {
        uint val = 0;
        res = res && device_read_byte(dev_id, addr, val);
        data = val;
        res = res && device_read_byte(dev_id, addr + 1, val);
        data |= (val << 8);
        res = res && device_read_byte(dev_id, addr + 2, val);
        data |= (val << 16);
        res = res && device_read_byte(dev_id, addr + 3, val);
        data |= (val << 24);
    }

    return res;
}

uint mulhu(uint a, uint b)
{
    uint a_high = a >> 16;
    uint a_low = a & 0xffff;
    uint b_high = b >> 16;
    uint b_low = b & 0xffff;

    uint high_high = a_high * b_high;
    uint high_low = a_high * b_low;
    uint low_high = a_low * b_high;
    uint low_low = a_low * b_low;

    uint mid_high = high_low + (low_low >> 16);
    uint mid = mid_high + low_high; /* could overflow, it's intended */

    uint carry = uint(mid < mid_high);

    uint res = high_high + (mid >> 16) + (carry << 16);

    return res;
}


uint mulh(int a, int b)
{
    uint ua = abs(a);
    uint ub = abs(b);

    uint res = mulhu(ua, ub);

    if (((a != 0) && (b != 0)) && ((a < 0) != (b < 0)))
    {
        res = ~res;
        uint low = ua * ub;
        if (low == 0)
        {
            res += 1;
        }
    }

    return res;
}


uint mulhsu(int a, uint b)
{
    uint ua = abs(a);
    uint res = mulhu(a, b);

    if ((b != 0) && (a < 0))
    {
        res = ~res;
        uint low = ua * b;
        if (low == 0)
        {
            res += 1;
        }
    }

    return res;
}



bool run_cycle(in uint dev_id)
{
    uint inst;

    if (!device_read_word(dev_id, cpus[dev_id].pc, inst))
    {
        return false;
    }

    uint opcode = inst & 0x7f;
    bool res = true;
    bool pc_updated = false;

    switch (opcode)
    {
    case 0x33: /*  Integer Register-Register ops */
    {
        uint rd = (inst >> 7) & 0x1f;
        uint funct3 = (inst >> 12) & 0x07;
        uint rs1 = (inst >> 15) & 0x1f;
        uint rs2 = (inst >> 20) & 0x1f;
        uint funct7 = (inst >> 25) & 0x7f;
    
        switch (funct3)
        {
        case 0x00:
            switch (funct7)
            {
            case 0x00: /* add */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] + cpus[dev_id].regs[rs2];
                break;

            case 0x20: /* sub */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] - cpus[dev_id].regs[rs2];
                break;

            case 0x01: /* mul */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] * cpus[dev_id].regs[rs2];
                break;

            default:
                res = false;
            }
            break;

        case 0x04:
            switch (funct7)
            {
            case 0x0: /* xor */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] ^ cpus[dev_id].regs[rs2];
                break;

            case 0x01: /* div */
                cpus[dev_id].regs[rd] = uint(int(cpus[dev_id].regs[rs1]) / int(cpus[dev_id].regs[rs2]));
                break;

            default:
                res = false;                   
            }
            break;

        case 0x06:
            switch (funct7)
            {
            case 0x0: /* or */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] | cpus[dev_id].regs[rs2];
                break;
            
            case 0x01: /* rem */
                cpus[dev_id].regs[rd] = uint(int(cpus[dev_id].regs[rs1]) % int(cpus[dev_id].regs[rs2]));
                break;

            default:
                res = false;
            }
            break;

        case 0x07:
            switch (funct7)
            {
            case 0x0: /* and */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] & cpus[dev_id].regs[rs2];
                break;                
            
            case 0x01: /* remu */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] % cpus[dev_id].regs[rs2];
                break;

            case 0x07: /* czero.nez */
                cpus[dev_id].regs[rd] = bool(cpus[dev_id].regs[rs2]) ? 0 : cpus[dev_id].regs[rs1];
                break;

            default:
                res = false;
            }
            break;

        case 0x01:
            switch (funct7)
            {
            case 0x00: /* sll */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] << cpus[dev_id].regs[rs2];
                break;

            case 0x01: /* mulh */
                cpus[dev_id].regs[rd] = mulh(int(cpus[dev_id].regs[rs1]), int(cpus[dev_id].regs[rs2]));
                break;

            default:
                res = false;
            }
            break;            

        case 0x05:
            switch (funct7)
            {
            case 0x00: /* srl */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] >> cpus[dev_id].regs[rs2];
                break;

            case 0x20: /* sra */
                cpus[dev_id].regs[rd] = int(cpus[dev_id].regs[rs1]) >> cpus[dev_id].regs[rs2];
                break;

            case 0x01: /* divu */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] / cpus[dev_id].regs[rs2];
                break;

            case 0x07: /* czero.eqz */
                cpus[dev_id].regs[rd] = bool(cpus[dev_id].regs[rs2]) ? cpus[dev_id].regs[rs1] : 0;                
                break;

            default:
                res = false;
            }
            break;

        case 0x02:
            switch (funct7)
            {
            case 0x00: /* slt */
                cpus[dev_id].regs[rd] = int(cpus[dev_id].regs[rs1]) < int(cpus[dev_id].regs[rs2]) ? 1 : 0;
                break;

            case 0x01: /* mulhsu */
                cpus[dev_id].regs[rd] = mulhsu(int(cpus[dev_id].regs[rs1]), cpus[dev_id].regs[rs2]);
                break;

            default:
                res = false;
            }
            break;

        case 0x03:
            switch (funct7)
            {
                case 0x00: /* sltu */
                    cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] < cpus[dev_id].regs[rs2] ? 1 : 0;
                    break;

                case 0x01: /* mulhu */
                    cpus[dev_id].regs[rd] = mulhu(cpus[dev_id].regs[rs1], cpus[dev_id].regs[rs2]);
                    break;

                default:
                    res = false;
            }            
            break;

        default:
            res = false;
            break;
        }
    }
    break;

    case 0x13: /* Integer Register-Immediate ops */
    {
        uint rd = (inst >> 7) & 0x1f;
        uint funct3 = (inst >> 12) & 0x07;
        uint rs1 = (inst >> 15) & 0x1f;
        int imm = int((inst >> 20) & 0xfff);
        imm = (imm << 20) >> 20;

        switch (funct3)
        {
        case 0x00:  /* addi */
            cpus[dev_id].regs[rd] = int(cpus[dev_id].regs[rs1]) + imm;
            break;

        case 0x04: /* xori */
            cpus[dev_id].regs[rd] = int(cpus[dev_id].regs[rs1]) ^ imm;
            break;

        case 0x06: /* ori */
            cpus[dev_id].regs[rd] = int(cpus[dev_id].regs[rs1]) | imm;
            break;

        case 0x07: /* andi */
            cpus[dev_id].regs[rd] = int(cpus[dev_id].regs[rs1]) & imm;
            break;

        case 0x01:
            switch (imm >> 5)
            {
            case 0x00: /* slli */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] << (imm & 0x1f);
                break;

            default:    
                res = false;
            }
            break;

        case 0x05:
            switch (imm >> 5)
            {
            case 0x00: /* srli */
                cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] >> (imm & 0x1f);
                break;

            case 0x20: /* srai */
                cpus[dev_id].regs[rd] = int(cpus[dev_id].regs[rs1]) >> (imm & 0x1f);
                break;

            default:
                res = false;
            }
            break;

        case 0x02: /* slti */
            cpus[dev_id].regs[rd] = int(cpus[dev_id].regs[rs1]) < imm ? 1 : 0;
            break;

        case 0x03: /* sltiu */
            cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] < (uint(imm) & 0xfff) ? 1 : 0;
            break;

        default:
            res = false;
            break;
        }

    }
    break;

    case 0x23: /* Store ops */
    {
        int imm = int((inst >> 7) & 0x1f);
        uint funct3 = (inst >> 12) & 0x07;
        uint rs1 = (inst >> 15) & 0x1f;
        uint rs2 = (inst >> 20) & 0x1f;
        imm |= int(((inst >> 25) & 0x7f) << 5);
        imm = (imm << 20) >> 20;
        
        uint addr = cpus[dev_id].regs[rs1] + imm;

        switch (funct3)
        {
        case 0x00: /* sb */
            device_write_byte(dev_id, addr, cpus[dev_id].regs[rs2] & 0xff);
            break;

        case 0x01: /* sh */
            device_write_hword(dev_id, addr, cpus[dev_id].regs[rs2] & 0xffff);
            break;

        case 0x02: /* sw */
            device_write_word(dev_id, addr, cpus[dev_id].regs[rs2]);
            break;

        default:
            res = false;
            break;
        }
    }
    break;

    case 0x03: /* Load ops */
    {
        uint rd = (inst >> 7) & 0x1f;
        uint funct3 = (inst >> 12) & 0x07;
        uint rs1 = (inst >> 15) & 0x1f;
        int imm = int((inst >> 20) & 0xfff);
        imm = (imm << 20) >> 20;
        uint addr = cpus[dev_id].regs[rs1] + imm;
        uint data;

        switch (funct3)
        {
        case 0x00: /* lb */
            res = device_read_byte(dev_id, addr, data);
            cpus[dev_id].regs[rd] = uint((int(data) << 24) >> 24);
            break;

        case 0x01: /* lh */
            res = device_read_hword(dev_id, addr, data);
            cpus[dev_id].regs[rd] = uint((int(data) << 16) >> 16);
            break;

        case 0x02: /* lw */
            res = device_read_word(dev_id, addr, data);
            cpus[dev_id].regs[rd] = data;
            break;

        case 0x04: /* lbu */
            res = device_read_byte(dev_id, addr, data);
            cpus[dev_id].regs[rd] = data;
            break;

        case 0x05: /* lhu */
            res = device_read_hword(dev_id, addr, data);
            cpus[dev_id].regs[rd] = data;
            break;

        default:
            res = false;
            break;
        }
    }
    break;

    case 0x63: /* Branching ops */
    {
        int imm = int((inst >> 8) & 0x0f);
        imm |= int(((inst >> 7) & 1) << 10);
        uint funct3 = (inst >> 12) & 0x07;
        uint rs1 = (inst >> 15) & 0x1f;
        uint rs2 = (inst >> 20) & 0x1f;
        imm |= int(((inst >> 25) & 0x3f) << 4);
        imm = int((imm << 1) | (((inst >> 31) & 1) << 12));
        imm = (imm << 19) >> 19;

        switch (funct3)
        {
        case 0x00: /* beq == */
            if (cpus[dev_id].regs[rs1] == cpus[dev_id].regs[rs2])
            {
                cpus[dev_id].pc += imm;
                pc_updated = true;
            }
            break;

        case 0x01: /* bne != */
            if (cpus[dev_id].regs[rs1] != cpus[dev_id].regs[rs2])
            {
                cpus[dev_id].pc += imm;
                pc_updated = true;
            }
            break;

        case 0x04: /* blt < */
            if (int(cpus[dev_id].regs[rs1]) < int(cpus[dev_id].regs[rs2]))
            {
                cpus[dev_id].pc += imm;
                pc_updated = true;
            }
            break;

        case 0x05: /* bge >= */
            if (int(cpus[dev_id].regs[rs1]) >= int(cpus[dev_id].regs[rs2]))
            {
                cpus[dev_id].pc += imm;
                pc_updated = true;
            }
            break;

        case 0x06: /* bltu < (u) */
            if (cpus[dev_id].regs[rs1] < cpus[dev_id].regs[rs2])
            {
                cpus[dev_id].pc += imm;
                pc_updated = true;
            }
            break;

        case 0x07: /* bgeu >= (u) */
            if (cpus[dev_id].regs[rs1] >= cpus[dev_id].regs[rs2])
            {
                cpus[dev_id].pc += imm;
                pc_updated = true;
            }
            break;

        default:
            res = false;
            break;
        }

    }
    break; 

    case 0x6f: /* jal */
    {
        uint rd = (inst >> 7) & 0x1f;
        int imm = int(((inst >> 21) & 0x3ff) << 1);
        imm |= int(((inst >> 20) & 1) << 11);
        imm |= int(((inst >> 12) & 0xff) << 12);
        imm |= int((inst >> 31) << 20);
        imm = (imm << 11) >> 11;

        cpus[dev_id].regs[rd] = cpus[dev_id].pc + 4;
        cpus[dev_id].pc += imm;
        pc_updated = true;
    }
    break;

    case 0x67: /* jalr */
    {
        uint rd = (inst >> 7) & 0x1f;
        uint funct3 = (inst >> 12) & 0x07;
        uint rs1 = (inst >> 15) & 0x1f;
        int imm = int((inst >> 20) & 0xfff);
        imm = (imm << 20) >> 20;

        switch (funct3)
        {
        case 0x00:
            cpus[dev_id].regs[rd] = cpus[dev_id].pc + 4;
            cpus[dev_id].pc = cpus[dev_id].regs[rs1] + imm;
            pc_updated = true;
        break;

        default:
            res = false;
            break;
        }
    }
    break;

    case 0x37: /* lui */
    {
        uint rd = (inst >> 7) & 0x1f;
        int imm = int((inst >> 12) & 0xfffff);
        imm = (imm << 11) >> 11;
        
        cpus[dev_id].regs[rd] = imm << 12;        
    }
    break;

    case 0x17: /* auipc */
    {
        uint rd = (inst >> 7) & 0x1f;
        int imm = int((inst >> 12) & 0xfffff);
        imm = (imm << 11) >> 11;

        cpus[dev_id].regs[rd] = cpus[dev_id].pc + (imm << 12);
    }
    break;

    case 0x73: /* Env call & breakpoint */
    {
        uint funct12 = (inst >> 20) & 0xfff;
        uint rs1 = (inst >> 15) & 0x1f;
        uint funct3 = (inst >> 12) & 0x07;
        uint rd = (inst >> 7) & 0x1f;
        res = (rs1 == funct3) == (rd == 0x0);

        switch (funct12)
        {
        case 0x00:
            /* Nothing for now */
            break;

        case 0x01:
            /* Nothing for now */
            break;

        default:
            res = false;
            break;
        }
    }
    break;

    default:
        res = false;
        break;
    }

    if (res && !pc_updated)
    {
        cpus[dev_id].pc += 4;
    }

    cpus[dev_id].regs[0] = 0;

    return res;
}


bool run_unpacked_instruction(in uint dev_id)
{
    bool res = true;
    bool pc_updated = false;

    uint inst_idx = (cpus[dev_id].pc - 0x08000000) >> 2;

    uint rd = uinsts[inst_idx].rd;
    uint rs1val = cpus[dev_id].regs[uinsts[inst_idx].rs1];
    uint rs2val = cpus[dev_id].regs[uinsts[inst_idx].rs2];
    int  imm = uinsts[inst_idx].imm;
    uint addr = rs1val + imm;
    uint data;

    switch (uinsts[inst_idx].inst_id)
    {
    case INST_NOP:
        break;

    case INST_ADD:
        cpus[dev_id].regs[rd] = rs1val + rs2val;
        break;

    case INST_SUB:
        cpus[dev_id].regs[rd] = rs1val - rs2val;
        break;

    case INST_MUL:
        cpus[dev_id].regs[rd] = rs1val * rs2val;
        break;

    case INST_XOR:
        cpus[dev_id].regs[rd] = rs1val ^ rs2val;
        break;

    case INST_DIV:
        cpus[dev_id].regs[rd] = uint(int(rs1val) / int(rs2val));
        break;

    case INST_OR:
        cpus[dev_id].regs[rd] = rs1val | rs2val;
        break;

    case INST_REM:
        cpus[dev_id].regs[rd] = uint(int(rs1val) % int(rs2val));
        break;

    case INST_AND:
        cpus[dev_id].regs[rd] = rs1val & rs2val;
        break;

    case INST_REMU:
        cpus[dev_id].regs[rd] = rs1val % rs2val;
        break;

    case INST_CZERO_NEZ:
        cpus[dev_id].regs[rd] = bool(rs2val) ? 0 : rs1val;
        break;

    case INST_SLL:
        cpus[dev_id].regs[rd] = rs1val << rs2val;
        break;

    case INST_MULH:
        cpus[dev_id].regs[rd] = mulh(int(rs1val), int(rs2val));
        break;

    case INST_SRL:
        cpus[dev_id].regs[rd] = rs1val >> rs2val;
        break;

    case INST_SRA:
        cpus[dev_id].regs[rd] = int(rs1val) >> rs2val;
        break;

    case INST_DIVU:
        cpus[dev_id].regs[rd] = rs1val / rs2val;
        break;

    case INST_CZERO_EQZ:
        cpus[dev_id].regs[rd] = bool(rs2val) ? rs1val : 0;
        break;

    case INST_SLT:
        cpus[dev_id].regs[rd] = int(rs1val) < int(rs2val) ? 1 : 0;
        break;

    case INST_MULHSU:
        cpus[dev_id].regs[rd] = mulhsu(int(rs1val), rs2val);
        break;

    case INST_SLTU:
        cpus[dev_id].regs[rd] = rs1val < rs2val ? 1 : 0;
        break;

    case INST_MULHU:
        cpus[dev_id].regs[rd] = mulhu(rs1val, rs2val);
        break;

    case INST_ADDI:
        cpus[dev_id].regs[rd] = int(rs1val) + imm;
        break;

    case INST_XORI:
        cpus[dev_id].regs[rd] = int(rs1val) ^ imm;
        break;

    case INST_ORI:
        cpus[dev_id].regs[rd] = int(rs1val) | imm;
        break;

    case INST_ANDI:
        cpus[dev_id].regs[rd] = int(rs1val) & imm;
        break;

    case INST_SLLI:
        cpus[dev_id].regs[rd] = rs1val << (imm & 0x1f);
        break;

    case INST_SRLI:
        cpus[dev_id].regs[rd] = rs1val >> (imm & 0x1f);
        break;

    case INST_SRAI:
        cpus[dev_id].regs[rd] = int(rs1val) >> (imm & 0x1f);
        break;

    case INST_SLTI:
        cpus[dev_id].regs[rd] = int(rs1val) < imm ? 1 : 0;
        break;

    case INST_SLTIU:
        cpus[dev_id].regs[rd] = rs1val < (uint(imm) & 0xfff) ? 1 : 0;
        break;

    case INST_SB:
        device_write_byte(dev_id, addr, rs2val & 0xff);
        break;

    case INST_SH:
        device_write_hword(dev_id, addr, rs2val & 0xffff);
        break;

    case INST_SW:
        device_write_word(dev_id, addr, rs2val);
        break;

    case INST_LB:
        res = device_read_byte(dev_id, addr, data);
        cpus[dev_id].regs[rd] = uint((int(data) << 24) >> 24);
        break;

    case INST_LH:
        res = device_read_hword(dev_id, addr, data);
        cpus[dev_id].regs[rd] = uint((int(data) << 16) >> 16);
        break;

    case INST_LW:
        res = device_read_word(dev_id, addr, data);
        cpus[dev_id].regs[rd] = data;
        break;

    case INST_LBU:
        res = device_read_byte(dev_id, addr, data);
        cpus[dev_id].regs[rd] = data;
        break;

    case INST_LHU:
        res = device_read_hword(dev_id, addr, data);
        cpus[dev_id].regs[rd] = data;
        break;

    case INST_BEQ:
        if (rs1val == rs2val)
        {
            cpus[dev_id].pc += imm;
            pc_updated = true;
        }
        break;

    case INST_BNE:
        if (rs1val != rs2val)
        {
            cpus[dev_id].pc += imm;
            pc_updated = true;
        }
        break;

    case INST_BLT:
        if (int(rs1val) < int(rs2val))
        {
            cpus[dev_id].pc += imm;
            pc_updated = true;
        }
        break;

    case INST_BGE:
        if (int(rs1val) >= int(rs2val))
        {
            cpus[dev_id].pc += imm;
            pc_updated = true;
        }
        break;

    case INST_BLTU:
        if (rs1val < rs2val)
        {
            cpus[dev_id].pc += imm;
            pc_updated = true;
        }
        break;

    case INST_BGEU:
        if (rs1val >= rs2val)
        {
            cpus[dev_id].pc += imm;
            pc_updated = true;
        }
        break;

    case INST_JAL:
        cpus[dev_id].regs[rd] = cpus[dev_id].pc + 4;
        cpus[dev_id].pc += imm;
        pc_updated = true;
        break;

    case INST_JALR:
        cpus[dev_id].regs[rd] = cpus[dev_id].pc + 4;
        cpus[dev_id].pc = rs1val + imm;
        pc_updated = true;
        break;

    case INST_LUI:
        cpus[dev_id].regs[rd] = imm << 12;
        break;

    case INST_AUIPC:
        cpus[dev_id].regs[rd] = cpus[dev_id].pc + (imm << 12);
        break;

    case INST_ECALL:
        /* Nothing for now */
        break;

    case INST_BREAK:
        /* Nothing for now */
        break;

    case INST_INVALID:
    default:
        res = false;
        break;
    }

    if (res && !pc_updated)
    {
        cpus[dev_id].pc += 4;
    }

    cpus[dev_id].regs[0] = 0;

    return false;
}


void main()
{
    uint dev_id = gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x + gl_GlobalInvocationID.x;

    for (uint i = 0; (i < n_cycles) && (cpus[dev_id].periph[9] == 0); i++)
    // for (uint i = 0; i < n_cycles; i++)
    {
        run_cycle(dev_id);
        // run_unpacked_instruction(dev_id);

        // if (0 != cpus[dev_id].periph[9])
        // {
        //     break;
        // }

        /* Update RTC if requested */
        if (0 != cpus[dev_id].periph[3])
        {
            cpus[dev_id].periph[3] = 0;
            // cpus[dev_id].periph[1] = time_ms;
            cpus[dev_id].periph[1] += uint(floor(abs(rand(vec2(float(dev_id), 42.0)) * 1000.0)));
        }
    }
}
