
#include "rv_emu.h"

static bool mem_write(mem_t *mem, uint32_t addr,
                      const uint8_t *data, uint32_t size);
static bool mem_read(mem_t *mem, uint32_t addr,
                     uint8_t *data, uint32_t size);

static void *ilp_thread_proc(void *arg);


void device_init(device_t *dev,
                 uint32_t rom_size, uint32_t rom_origin,
                 uint32_t ram_size, uint32_t ram_origin,
                 uint32_t periph_size, uint32_t periph_origin)
{
    memset(dev, 0, sizeof(device_t));
    dev->rom.origin = rom_origin;
    dev->rom.size = rom_size;
    dev->rom.data = malloc(rom_size);
    memset(dev->rom.data, 0, rom_size);

    dev->ram.origin = ram_origin;
    dev->ram.size = ram_size;
    dev->ram.data = malloc(ram_size);
    memset(dev->ram.data, 0, ram_size);

    dev->periph.origin = periph_origin;
    dev->periph.size = periph_size;
    dev->periph.data = malloc(periph_size);
    memset(dev->periph.data, 0, periph_size);

    dev->pc = dev->rom.origin;
}


void device_uninit(device_t *dev)
{
    free(dev->rom.data);
    free(dev->ram.data);
    free(dev->periph.data);

    if (dev->ilp_map || dev->ilp_table)
    {
        free(dev->ilp_map);
        free(dev->ilp_table);
    }

    memset(dev, 0, sizeof(device_t));
}


bool device_load_from_elf(device_t *dev, const char *elf_file_name)
{
    FILE *elf = fopen(elf_file_name, "rb");

    if (!elf)
    {
        printf("Error: unable to open '%s'\n", elf_file_name);
        return false;
    }

    elf_hdr_t elf_hdr = {0};
    size_t rs;

    rs = fread(&elf_hdr, 1, sizeof(elf_hdr), elf);
    
    if (rs != sizeof(elf_hdr))
    {
        fclose(elf);
        return false;
    }

    if (elf_hdr.machine != 0x00f3 || elf_hdr.e_ident.bitness != 1)
    {
        printf("Error: this ELF file is not RISC-V 32bit\n");
        fclose(elf);
        return false;
    }

    fseek(elf, elf_hdr.shoff, SEEK_SET);

    sec_hdr_t *sec_table = malloc(sizeof(sec_hdr_t) * elf_hdr.shnum);
    rs = fread(sec_table, sizeof(sec_hdr_t), elf_hdr.shnum, elf);
    
    for (int i = 0; i < elf_hdr.shnum; i++)
    {
        if (sec_table[i].type == 1) /* SHT_PROGBITS */
        {
            fseek(elf, sec_table[i].offset, SEEK_SET);

            printf("Writing block of size %u to RAM addr: 0x%08X\n",
                   sec_table[i].size, sec_table[i].addr);
            for (int j = 0; j < sec_table[i].size; j++)
            {
                uint8_t b;
                rs = fread(&b, 1, 1, elf);
                if (!device_write(dev, sec_table[i].addr + j, &b, 1))
                {
                    printf("Error writing to the device address: 0x%08X\n",
                           sec_table[i].addr + j);
                    break;
                }
            }
        }
    }

    int strtab_id = -1;
    int symtab_id = -1;

    /* Find string and symbol table indices */
    for (int i = 0; i < elf_hdr.shnum; i++)
    {
        fseek(elf,
              sec_table[elf_hdr.shstrndx].offset + sec_table[i].name, SEEK_SET);
        char sname[16] = {0};
        rs = fread(sname, 1, sizeof(".strtab"), elf);

        if (sec_table[i].type == 0x03 && !strcmp(".strtab", sname))
        {
            strtab_id = i;
        }
        else if (sec_table[i].type == 0x02 && !strcmp(".symtab", sname))
        {
            symtab_id = i;
        }
    }

    /* Find the _exit symbol address */
    fseek(elf, sec_table[symtab_id].offset, SEEK_SET);
    sym_t *symbols = malloc(sec_table[symtab_id].size);
    rs = fread(symbols, 1, sec_table[symtab_id].size, elf);

    for (int i = 0; i < sec_table[symtab_id].size / sizeof(sym_t); i++)
    {
        if ((symbols[i].info & 0x0f) == 0x02) /* STT_FUNC */
        {
            char sname[200] = {0};
            int ssize = 0;
            char c = 0;
            fseek(elf, sec_table[strtab_id].offset + symbols[i].name, SEEK_SET);
            do
            {
                rs = fread(&c, 1, 1, elf);
                sname[ssize++] = c;
            }
            while(c);

            if (!strcmp("_exit", sname))
            {
                dev->exit_addr = symbols[i].value;
                printf("_exit address: 0x%08X\n", dev->exit_addr);
                break;
            }
        }
    }

    fclose(elf);
    free(sec_table);
    free(symbols);

    return true;
}


bool device_load_ilp_table(device_t *dev, const char *ilp_file_name)
{
    FILE *ilp = fopen(ilp_file_name, "rb");

    if (!ilp)
    {
        printf("Error: unable to open '%s'\n", ilp_file_name);
        return false;
    }

    char magic[4] = {0};
    size_t rs;
    rs = fread(magic, 1, sizeof(magic), ilp);

    if (rs == sizeof(magic) && memcmp(magic, "ILP", sizeof(magic)))
    {
        printf("Error: Invalid ILP file\n");
        fclose(ilp);
        return false;
    }

    uint32_t num_blocks = 0;
    uint32_t num_threads = 0;
    uint32_t table_size = 0;

    rs = fread(&num_blocks, 1, sizeof(num_blocks), ilp);
    printf("Number of ILP blocks: %d\n", num_blocks);

    rs = fread(&num_threads, 1, sizeof(num_threads), ilp);
    printf("Number of threads: %d\n", num_threads);
    
    dev->ilp_n_blocks = num_blocks;
    dev->ilp_n_threads = num_threads;
    dev->ilp_map = malloc(sizeof(ilp_entry_t) * num_blocks);

    for (int i = 0; i < num_blocks; i++)
    {
        rs = fread(&dev->ilp_map[i], 1, sizeof(ilp_entry_t), ilp);
        table_size += dev->ilp_map[i].size;
        
        // printf("block %05u, start: 0x%08X, offset: %u, size: %u\n",
        //        i, dev->ilp_map[i].addr, dev->ilp_map[i].offset, dev->ilp_map[i].size);
    }

    printf("ILP table size: %u\n", table_size);

    dev->ilp_table = malloc(table_size);
    int read_table_size = fread(dev->ilp_table, 1, table_size, ilp);

    if (read_table_size != table_size)
    {
        printf("Error: Malformed ILP file\n");
        fclose(ilp);
        return false;
    }

    // int block_id = 6;

    // uint32_t block_size = dev->ilp_map[block_id].size;
    // uint32_t block_iid = dev->ilp_map[block_id].offset / 4;
    // printf("ILP block %u:\n", block_id);

    // while (block_size)
    // {
    //     uint32_t inst = dev->ilp_table[block_iid];
    //     printf("0x%08X ", inst);
    //     block_iid++;
    //     if (inst == 0x0)
    //     {
    //         printf("\n");
    //     }
    //     block_size -= 4;
    // }


    int res = 0;

    res = pthread_barrier_init(&dev->ilp_barrier1, NULL, num_threads + 1);
    res = pthread_barrier_init(&dev->ilp_barrier2, NULL, num_threads + 1);

    if (res)
    {
        printf("Error: initializing barrier: %d\n", res);
        return false;
    }

    dev->ilp_threads = malloc(sizeof(pthread_t) * num_threads);
    dev->ilp_threads_data = malloc(sizeof(ilp_thread_data_t) * num_threads);
    dev->ilp_slice = malloc(sizeof(uint32_t) * num_threads);

    for (int i = 0; i < dev->ilp_n_threads; i++)
    {
        dev->ilp_threads_data[i].dev = (struct device_t*)dev;
        dev->ilp_threads_data[i].thread_id = i;

        res = pthread_create(&dev->ilp_threads[i], NULL,
                             ilp_thread_proc, &dev->ilp_threads_data[i]);
    }

    fclose(ilp);
    return true;
}


static bool mem_write(mem_t *mem, uint32_t addr,
                      const uint8_t *data, uint32_t size)
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


bool device_write(device_t *dev, uint32_t addr,
                  const uint8_t *data, uint32_t size)
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


bool device_run_instruction(device_t *dev, uint32_t inst, uint32_t pc_ro)
{
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
        uint32_t rs2 = (inst >> 20) & 0b11111;
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

            case 0x01: /* mul */
                {
                    int32_t prod = (int32_t)dev->regs[rs1] * (int32_t)dev->regs[rs2];
                    device_set_reg(dev, rd, (uint32_t)prod);
                }
                break;

            default:
                res = false;
            }
            break;

        case 0x04: 
            switch (funct7)
            {
            case 0x0: /* xor */
                device_set_reg(dev, rd, dev->regs[rs1] ^ dev->regs[rs2]);
                break;

            case 0x01: /* div */
                device_set_reg(dev, rd, (uint32_t)((int32_t)dev->regs[rs1] / \
                                                   (int32_t)dev->regs[rs2]));
                break;

            default:
                break;
            }
            break;


        case 0x06:
            switch (funct7)
            {
            case 0x0: /* or */
                device_set_reg(dev, rd, dev->regs[rs1] | dev->regs[rs2]);
                break;
            
            case 0x01: /* rem */
                device_set_reg(dev, rd, (uint32_t)((int32_t)dev->regs[rs1] % \
                                                   (int32_t)dev->regs[rs2]));
                break;

            default:
                break;
            }
            break;

      
        case 0x07: 
            switch (funct7)
            {
            case 0x0: /* and */
                device_set_reg(dev, rd, dev->regs[rs1] & dev->regs[rs2]);
                break;                
            
            case 0x01: /* remu */
                device_set_reg(dev, rd, dev->regs[rs1] % dev->regs[rs2]);
                break;

            case 0x07: /* czero.nez */
                device_set_reg(dev, rd, dev->regs[rs2] ? 0 : dev->regs[rs1]);
                break;

            default:
                break;
            }
            break;


        case 0x01:
            switch (funct7)
            {
            case 0x00: /* sll */
                device_set_reg(dev, rd, dev->regs[rs1] << dev->regs[rs2]);
                break;

            case 0x01: /* mulh */
                {
                    int64_t op1 = (int64_t)(int32_t)dev->regs[rs1];
                    int64_t op2 = (int64_t)(int32_t)dev->regs[rs2];
                    int64_t prod = op1 * op2;
                    device_set_reg(dev, rd, (uint32_t)(prod >> 32));
                }
                break;

            default:
                res = false;
            }
            break;

        case 0x05:
            switch (funct7)
            {
            case 0x00: /* srl */
                device_set_reg(dev, rd, dev->regs[rs1] >> dev->regs[rs2]);
                break;

            case 0x20: /* sra */
                device_set_reg(dev, rd,
                               (int32_t)dev->regs[rs1] >> dev->regs[rs2]);
                break;

            case 0x01: /* divu */
                device_set_reg(dev, rd, dev->regs[rs1] / dev->regs[rs2]);
                break;

            case 0x07: /* czero.eqz */
                device_set_reg(dev, rd, dev->regs[rs2] ? dev->regs[rs1] : 0);
                break;

            default:
                res = false;
            }
            break;

        case 0x02:
            switch (funct7)
            {
            case 0x00: /* slt */
                device_set_reg(dev, rd, (int32_t)dev->regs[rs1] < \
                                        (int32_t)dev->regs[rs2] ? 1 : 0);
                break;

            case 0x01: /* mulhsu */
                {
                    int64_t prod = (int64_t)(int32_t)dev->regs[rs1] * (uint64_t)dev->regs[rs2];
                    device_set_reg(dev, rd, (uint32_t)(prod >> 32));
                }
                break;

            default:
                res = false;
            }
            break;

        case 0x03: 
            switch (funct7)
            {
                case 0x00: /* sltu */
                    device_set_reg(dev, rd, dev->regs[rs1] < dev->regs[rs2] ? 1 : 0);
                    break;

                case 0x01: /* mulhu */
                    {
                        uint64_t prod = (uint64_t)dev->regs[rs1] * (uint64_t)dev->regs[rs2];
                        device_set_reg(dev, rd, (uint32_t)(prod >> 32));
                    }
                    break;

                default:
                    res = false;
            }
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
                device_set_reg(dev, rd,
                               ((int32_t)(dev->regs[rs1]) >> (imm & 0b11111)));
                break;

            default:
                res = false;
            }
            break;

        case 0x02: /* slti */
            device_set_reg(dev, rd, (int32_t)dev->regs[rs1] < imm ? 1 : 0);
            break;

        case 0x03: /* sltiu */
            device_set_reg(dev, rd, dev->regs[rs1] < \
                                    ((uint32_t)imm & 0b111111111111) ? 1 : 0);
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
            res = device_write(dev, addr, (uint8_t*)&dev->regs[rs2], 4);
            // printf("sw: addr: 0x%08X data: 0x%08X\n", addr, dev->regs[rs2]);

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
            uint32_t w;
            res = device_read(dev, addr, (uint8_t*)&w, 4);
            device_set_reg(dev, rd, w);
            // printf("lw: addr: 0x%08X data: 0x%08X\n", addr, w);
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

        device_set_reg(dev, rd, pc_ro + 4);
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
            device_set_reg(dev, rd, pc_ro + 4);
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
        
        device_set_reg(dev, rd, pc_ro + (imm << 12));
    }
    break;

    case 0b1110011: /* Env call & breakpoint */
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
        printf("Error: failed executing instruction: "
               "0x%08X at address 0x%08X\n", inst, dev->pc);
    }

    if (res && !pc_updated)
    {
        dev->pc += 4;
    }

    dev->regs[0] = 0;

    return res;    
}


static void *ilp_thread_proc(void *arg)
{
    device_t *dev = (struct device_t*)((ilp_thread_data_t*)arg)->dev;
    uint32_t id = ((ilp_thread_data_t*)arg)->thread_id;
    uint32_t addr;
    uint32_t inst;

    // for (;;)
    // {
    //     pthread_barrier_wait(&dev->ilp_barrier1);

    //     pthread_barrier_wait(&dev->ilp_barrier2);
    // }

    return NULL;
}


#define SINGLE_THREADED 1

bool device_run_cycle(device_t *dev)
{
    uint32_t addr, inst;
    bool res = true;

    if (dev->ilp_cur_items == 0 && dev->ilp_map != NULL)
    {
        uint32_t b_id = (dev->pc - dev->rom.origin) >> 2;
        
        if (b_id < dev->ilp_n_blocks)
        {
            dev->ilp_cur_id = dev->ilp_map[b_id].offset >> 2;
            dev->ilp_cur_items = dev->ilp_map[b_id].size >> 2;
        }

    }

    bool slice_end = false;

    if (dev->ilp_cur_items)
    {
        for (int i = 0; i < dev->ilp_n_threads; i++)
        {
            if (!slice_end)
            {
                addr = dev->ilp_table[dev->ilp_cur_id];
                dev->ilp_cur_id++;
                dev->ilp_cur_items--;
                slice_end = addr == 0x0;
            }

            dev->ilp_slice[i] = addr;
        }

        if (!slice_end && dev->ilp_cur_items)
        {
            dev->ilp_cur_id++;
            dev->ilp_cur_items--;
        }

#ifdef SINGLE_THREADED
        
        for (int i = 0; i < dev->ilp_n_threads; i++)
        {
            addr = dev->ilp_slice[i];

            if (addr)
            {
                if (!device_read(dev, addr, (uint8_t*)&inst, sizeof(inst)))
                {
                    return false;
                }

                res = res && device_run_instruction(dev, inst, addr);
            }
        }
#else

        // pthread_barrier_wait(&dev->ilp_barrier1);
        // pthread_barrier_wait(&dev->ilp_barrier2);

#endif
    }
    else
    {
        if (!device_read(dev, dev->pc, (uint8_t*)&inst, sizeof(inst)))
        {
            return false;
        }
        
        res = device_run_instruction(dev, inst, dev->pc);
    }

    return res;
}
