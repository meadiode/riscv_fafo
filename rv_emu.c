
#include "rv_emu.h"

static bool mem_write(mem_t *mem, uint32_t addr, const uint8_t *data, uint32_t size);
static bool mem_read(mem_t *mem, uint32_t addr, uint8_t *data, uint32_t size);


void device_init(device_t *dev,
                 uint32_t rom_size, uint32_t rom_origin,
                 uint32_t ram_size, uint32_t ram_origin,
                 uint32_t periph_size, uint32_t periph_origin)
{
    memset(dev, 0, sizeof(device_t));
    dev->rom.origin = rom_origin;
    dev->rom.size = rom_size;
    dev->rom.data = malloc(rom_size);

    dev->ram.origin = ram_origin;
    dev->ram.size = ram_size;
    dev->ram.data = malloc(ram_size);

    dev->periph.origin = periph_origin;
    dev->periph.size = periph_size;
    dev->periph.data = malloc(periph_size);

    dev->pc = dev->rom.origin;
}


void device_uninit(device_t *dev)
{
    free(dev->rom.data);
    free(dev->ram.data);
    free(dev->periph.data);
    memset(dev, 0, sizeof(device_t));
}


static bool mem_write(mem_t *mem, uint32_t addr, const uint8_t *data, uint32_t size)
{
    if (addr >= mem->origin)
    {
        uint32_t offset = addr - mem->origin;
        if ((offset + size) <= mem->size)
        {
            memcpy(mem->data + offset, data, size);
            return true;
        }
    }
    return false;
}


static bool mem_read(mem_t *mem, uint32_t addr, uint8_t *data, uint32_t size)
{
    if (addr >= mem->origin)
    {
        uint32_t offset = addr - mem->origin;
        if ((offset + size) <= mem->size)
        {
            memcpy(data, mem->data + offset, size);
            return true;
        }
    }
    return false;
}



bool device_write(device_t *dev, uint32_t addr, const uint8_t *data, uint32_t size)
{
    return mem_write(&dev->ram, addr, data, size) ||
           mem_write(&dev->rom, addr, data, size) ||
           mem_write(&dev->periph, addr, data, size);
}


bool device_read(device_t *dev, uint32_t addr, uint8_t *data, uint32_t size)
{
    return mem_read(&dev->ram, addr, data, size) ||
           mem_read(&dev->rom, addr, data, size) ||
           mem_read(&dev->periph, addr, data, size);
}


void device_set_reg(device_t *dev, int rd, uint32_t val)
{
    dev->regs[rd] = val;
}


bool device_run_cycle(device_t *dev)
{
    uint32_t inst;

    if (!device_read(dev, dev->pc, (uint8_t*)&inst, sizeof(inst)))
    {
        return false;
    }

    // printf("Executing 0x%08X at 0x%08X\n", inst, dev->pc);

    uint32_t opcode = inst & 0b1111111;
    bool res = true;
    bool pc_updated = false;

    switch (opcode)
    {
    case 0b0110011:  /* Integer Register-Register ops */
    {
        uint32_t rd = (inst >> 7) & 0b11111;
        uint32_t funct3 = (inst >> 12) & 0b111;
        uint32_t rs1 = (inst >> 15) & 0b11111;
        uint32_t rs2 = (inst >> 20) & 0b11111;;
        uint32_t funct7 = (inst >> 25) & 0b1111111;

        switch (funct3)
        {
        case 0x00:
            switch (funct7)
            {
            case 0x00: /* add */
                device_set_reg(dev, rd, dev->regs[rs1] + dev->regs[rs2]);
                break;

            case 0x20: /* sub */
                device_set_reg(dev, rd, dev->regs[rs1] - dev->regs[rs2]);
                break;

            default:
                res = false;
            }
            break;

        case 0x04: /* xor */
            device_set_reg(dev, rd, dev->regs[rs1] ^ dev->regs[rs2]);
            res = funct7 == 0x00;
            break;

        case 0x06: /* or */
            device_set_reg(dev, rd, dev->regs[rs1] | dev->regs[rs2]);
            res = funct7 == 0x00;
            break;

        case 0x07: /* and */
            device_set_reg(dev, rd, dev->regs[rs1] & dev->regs[rs2]);
            res = funct7 == 0x00;
            break;

        case 0x01: /* sll */
            device_set_reg(dev, rd, dev->regs[rs1] << dev->regs[rs2]);
            res = funct7 == 0x00;
            break;

        case 0x05:
            switch (funct7)
            {
            case 0x00: /* srl */
                device_set_reg(dev, rd, dev->regs[rs1] >> dev->regs[rs2]);
                break;

            case 0x20: /* sra */
                device_set_reg(dev, rd, (int32_t)dev->regs[rs1] >> dev->regs[rs2]);
                break;

            default:
                res = false;
            }
            break;

        case 0x02: /* slt */
            device_set_reg(dev, rd, (int32_t)dev->regs[rs1] < (int32_t)dev->regs[rs2] ? 1 : 0);
            res = funct7 == 0x00;
            break;

        case 0x03: /* sltu */
            device_set_reg(dev, rd, dev->regs[rs1] < dev->regs[rs2] ? 1 : 0);
            res = funct7 == 0x00;
            break;

        default:
            printf("Error: invalid R-type instruction 0x%08X\n", inst);
            res = false;
            break;
        }
    }
    break;

    case 0b0010011: /* Integer Register-Immediate ops */
    {
        uint32_t rd = (inst >> 7) & 0b11111;
        uint32_t funct3 = (inst >> 12) & 0b111;
        uint32_t rs1 = (inst >> 15) & 0b11111;
        int32_t imm = (inst >> 20) & 0b111111111111;
        imm = (imm << 20) >> 20;

        switch (funct3)
        {
        case 0x00:  /* addi */
            device_set_reg(dev, rd, (int32_t)dev->regs[rs1] + imm);
            break;

        case 0x04: /* xori */
            device_set_reg(dev, rd, (int32_t)dev->regs[rs1] ^ imm);
            break;

        case 0x06: /* ori */
            device_set_reg(dev, rd, (int32_t)dev->regs[rs1] | imm);
            break;

        case 0x07: /* andi */
            device_set_reg(dev, rd, (int32_t)dev->regs[rs1] & imm);
            break;

        case 0x01:
            switch (imm >> 5)
            {
            case 0x00: /* slli */
                device_set_reg(dev, rd, dev->regs[rs1] << (imm & 0b11111));
                break;

            default:    
                res = false;
            }
            break;

        case 0x05:
            switch (imm >> 5)
            {
            case 0x00: /* srli */
                device_set_reg(dev, rd, dev->regs[rs1] >> (imm & 0b11111));
                break;

            case 0x20: /* srai */
                device_set_reg(dev, rd, ((int32_t)(dev->regs[rs1]) >> (imm & 0b11111)));
                break;

            default:
                res = false;
            }
            break;

        case 0x02: /* slti */
            device_set_reg(dev, rd, (int32_t)dev->regs[rs1] < imm ? 1 : 0);
            break;

        case 0x03: /* sltiu */
            device_set_reg(dev, rd, dev->regs[rs1] < ((uint32_t)imm & 0x111111111111) ? 1 : 0);
            break;

        default:
            printf("Error: invalid I-type instruction 0x%08X\n", inst);
            res = false;
            break;
        }   
    }
    break;

    case 0b0100011: /* Store ops */
    {
        int32_t imm = (inst >> 7) & 0b11111;
        uint32_t funct3 = (inst >> 12) & 0b111;
        uint32_t rs1 = (inst >> 15) & 0b11111;
        uint32_t rs2 = (inst >> 20) & 0b11111;
        imm |= ((inst >> 25) & 0b1111111) << 5;
        imm = (imm << 20) >> 20;
        
        uint32_t addr = (int32_t)dev->regs[rs1] + imm;

        switch (funct3)
        {
        case 0x00: /* sb */
            uint8_t bt = dev->regs[rs2] & 0xff;
            res = device_write(dev, addr, &bt, 1);
            break;

        case 0x01: /* sh */
            uint16_t hw = dev->regs[rs2] & 0xffff;
            res = device_write(dev, addr, (uint8_t*)&hw, 2);
            break;

        case 0x02: /* sw */
            // printf("Addr: 0x%08X\n", addr);
            res = device_write(dev, addr, (uint8_t*)&dev->regs[rs2], 4);
            break;

        default:
            printf("Error: invalid S-type instruction 0x%08X\n", inst);
            res = false;
            break;
        }
    }
    break;

    case 0b0000011: /* Load ops */
    {
        uint32_t rd = (inst >> 7) & 0b11111;
        uint32_t funct3 = (inst >> 12) & 0b111;
        uint32_t rs1 = (inst >> 15) & 0b11111;
        int32_t imm = (inst >> 20) & 0b111111111111;
        imm = (imm << 20) >> 20;
        uint32_t addr = dev->regs[rs1] + imm;

        switch (funct3)
        {
        case 0x00: /* lb */
            int8_t sb;
            res = device_read(dev, addr, (uint8_t*)&sb, 1);
            device_set_reg(dev, rd, (int32_t)sb);
            break;

        case 0x01: /* lh */
            int16_t shw;
            res = device_read(dev, addr, (uint8_t*)&shw, 2);
            device_set_reg(dev, rd, (int32_t)shw);
            break;

        case 0x02: /* lw */
            // printf("Addr: 0x%08X\n", addr);
            uint32_t w;
            res = device_read(dev, addr, (uint8_t*)&w, 4);
            device_set_reg(dev, rd, w);
            break;

        case 0x04: /* lbu */
            uint8_t ub;
            res = device_read(dev, addr, &ub, 1);
            device_set_reg(dev, rd, ub);
            break;

        case 0x05: /* lhu */
            uint16_t hw;
            res = device_read(dev, addr, (uint8_t*)&hw, 2);
            device_set_reg(dev, rd, hw);
            break;

        default:
            printf("Error: invalid I-type instruction 0x%08X\n", inst);
            res = false;
            break;
        }
    }
    break;

    case 0b1100011: /* Branching ops */
    {
        int32_t imm = (inst >> 8) & 0b1111;
        imm |= ((inst >> 7) & 1) << 10;
        uint32_t funct3 = (inst >> 12) & 0b111;
        uint32_t rs1 = (inst >> 15) & 0b11111;
        uint32_t rs2 = (inst >> 20) & 0b11111;
        imm |= ((inst >> 25) & 0b111111) << 4;
        imm = (imm << 1) | (((inst >> 31) & 1) << 12);
        imm = (imm << 19) >> 19;

        switch (funct3)
        {
        case 0x00: /* beq == */
            if ((int32_t)(dev->regs[rs1]) == (int32_t)(dev->regs[rs2]))
            {
                dev->pc += imm;
                pc_updated = true;
            }
            break;

        case 0x01: /* bne != */
            if ((int32_t)(dev->regs[rs1]) != (int32_t)(dev->regs[rs2]))
            {
                dev->pc += imm;
                pc_updated = true;
            }
            break;

        case 0x04: /* blt < */
            if ((int32_t)(dev->regs[rs1]) < (int32_t)(dev->regs[rs2]))
            {
                dev->pc += imm;
                pc_updated = true;
            }
            break;

        case 0x05: /* bge >= */
            if ((int32_t)(dev->regs[rs1]) >= (int32_t)(dev->regs[rs2]))
            {
                dev->pc += imm;
                pc_updated = true;
            }
            break;

        case 0x06: /* bltu < (u) */
            if (dev->regs[rs1] < dev->regs[rs2])
            {
                dev->pc += imm;
                pc_updated = true;
            }
            break;

        case 0x07: /* bgeu >= (u) */
            if (dev->regs[rs1] >= dev->regs[rs2])
            {
                dev->pc += imm;
                pc_updated = true;
            }
            break;

        default:
            printf("Error: invalid B-type instruction 0x%08X\n", inst);
            res = false;
            break;
        }

    }
    break;

    case 0b1101111: /* jal */
    {
        uint32_t rd = (inst >> 7) & 0b11111;

        int32_t imm = ((inst >> 21) & 0b1111111111) << 1;
        imm |= ((inst >> 20) & 1) << 11;
        imm |= ((inst >> 12) & 0b11111111) << 12;
        imm |= (inst >> 31) << 20;
        imm = (imm << 11) >> 11;

        device_set_reg(dev, rd, dev->pc + 4);
        dev->pc += imm;
        pc_updated = true;
    }
    break;

    case 0b1100111: /* jalr */
    {
        uint32_t rd = (inst >> 7) & 0b11111;
        uint32_t funct3 = (inst >> 12) & 0b111;
        uint32_t rs1 = (inst >> 15) & 0b11111;
        int32_t imm = (inst >> 20) & 0b111111111111;
        imm = (imm << 20) >> 20;

        switch (funct3)
        {
        case 0x00:
            device_set_reg(dev, rd, dev->pc + 4);
            dev->pc = dev->regs[rs1] + imm;
            pc_updated = true;
        break;

        default:
            printf("Error: invalid I-type instruction 0x%08X\n", inst);
            res = false;
            break;
        }
    }
    break;

    case 0b0110111: /* lui */
    {
        uint32_t rd = (inst >> 7) & 0b11111;
        int32_t imm = (inst >> 12) & 0xfffff;
        imm = (imm << 11) >> 11;
        
        device_set_reg(dev, rd, imm << 12);
    }
    break;

    case 0b0010111: /* auipc */
    {
        uint32_t rd = (inst >> 7) & 0b11111;
        int32_t imm = (inst >> 12) & 0xfffff;
        imm = (imm << 11) >> 11;

        dev->regs[rd] = dev->pc + (imm << 12);
    }
    break;

    case 0b1110011:
    {
        uint32_t funct12 = (inst >> 20) & 0b111111111111;
        uint32_t rs1 = (inst >> 15) & 0b11111;
        uint32_t funct3 = (inst >> 12) & 0b111;
        uint32_t rd = (inst >> 7) & 0b11111;
        res = (rs1 == funct3) == (rd == 0x0);

        switch (funct12)
        {
        case 0x00:
            // printf("<<< ECALL >>>\n");
            break;

        case 0x01:
            // printf("<<< BREAK >>>\n");
            break;

        default:
            printf("Error: invalid I-type instruction 0x%08X\n", inst);
            res = false;
            break;
        }
    }
    break;

    default:
        printf("Error: unrecognized opcode 0x%02X\n", opcode);
        res = false;
        break;
    }

    if (!res)
    {
        printf("Error: failed executing instruction: 0x%08X at address 0x%08X\n", inst, dev->pc);
    }

    if (res && !pc_updated)
    {
        dev->pc += 4;
    }
    dev->regs[0] = 0;
    dev->elapsed_cycles++;

    return res;
}
