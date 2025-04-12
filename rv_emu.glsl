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
    uint periph[10];
};

layout (std430, binding = 1) buffer rv_cpus_layout {
    cpu_t cpus[];
};

layout (std430, binding = 2) readonly buffer rv_rom_layout {
    uint rom[];
};

layout (std430, binding = 3) buffer rv_ram_layout {
    uint ram[];
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

            default:
                res = false;
            }
            break;

        case 0x04: /* xor */
            cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] ^ cpus[dev_id].regs[rs2];
            res = funct7 == 0x00;
            break;

        case 0x06: /* or */
            cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] | cpus[dev_id].regs[rs2];
            res = funct7 == 0x00;
            break;

        case 0x07: /* and */
            cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] & cpus[dev_id].regs[rs2];
            res = funct7 == 0x00;
            break;

        case 0x01: /* sll */
            cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] << cpus[dev_id].regs[rs2];
            res = funct7 == 0x00;
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

            default:
                res = false;
            }
            break;

        case 0x02: /* slt */
            cpus[dev_id].regs[rd] = int(cpus[dev_id].regs[rs1]) < int(cpus[dev_id].regs[rs2]) ? 1 : 0;
            res = funct7 == 0x00;
            break;

        case 0x03: /* sltu */
            cpus[dev_id].regs[rd] = cpus[dev_id].regs[rs1] < cpus[dev_id].regs[rs2] ? 1 : 0;
            res = funct7 == 0x00;
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


void main()
{
    uint dev_id = gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x + gl_GlobalInvocationID.x;

    for (uint i = 0; (i < n_cycles) && (cpus[dev_id].periph[9] == 0); i++)
    {
        run_cycle(dev_id);

        if (0 != cpus[dev_id].periph[9])
        {
            break;
        }

        /* Update RTC if requested */
        if (0 != cpus[dev_id].periph[3])
        {
            cpus[dev_id].periph[3] = 0;
            cpus[dev_id].periph[1] = time_ms;
        }
    }
}
