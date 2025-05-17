
#include "rv_emu.h"

static bool mem_write(mem_t *mem, uint32_t addr,
                      const uint8_t *data, uint32_t size);
static bool mem_read(mem_t *mem, uint32_t addr,
                     uint8_t *data, uint32_t size);

static void *ilp_thread_proc(void *arg);
static bool unpack_instruction(uint32_t inst, uinst_t *uinst);
static bool device_run_unpacked_instruction(device_t *dev, uinst_t inst, uint32_t pc_ro);

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

            if (sec_table[i].flags & 0x04)
            {
                uint32_t sec_end = sec_table[i].addr + sec_table[i].size;

                if (sec_end > dev->prog_end)
                {
                    dev->prog_end = sec_end;
                }
            }

        }
    }

    printf("Program end: 0x%08X\n", dev->prog_end);

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


bool device_pre_unpack_instructions(device_t *dev)
{
    if (!dev->prog_end || !(dev->prog_end > dev->rom.origin &&
                            dev->prog_end <= (dev->rom.origin + dev->rom.size)))
    {
        return false;
    }

    uint32_t num_insts = (dev->prog_end - dev->rom.origin) / 4;
    uint32_t pc = dev->rom.origin;
    uint32_t inst;

    dev->uinsts = malloc(num_insts * sizeof(uinst_t));

    if (!dev->uinsts)
    {
        return false;
    }

    for (int i = 0; i < num_insts; i++)
    {
        device_read(dev, pc, (uint8_t*)&inst, sizeof(inst));
        unpack_instruction(inst, dev->uinsts + i);
        pc += 4;
    }

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
    uinst_t uinst;

    if (!unpack_instruction(inst, &uinst))
    {
        printf("Error: failed executing instruction: "
               "0x%08X at address 0x%08X\n", inst, dev->pc);
        
        return false;
    }

    return device_run_unpacked_instruction(dev, uinst, pc_ro);
}


static bool unpack_instruction(uint32_t inst, uinst_t *uinst)
{
    uint32_t opcode = inst & 0b1111111;
    bool res = true;
    uinst->inst_id = INST_INVALID;

    switch (opcode)
    {
    case 0b0110011:  /* Integer Register-Register ops */
    {
        uint32_t funct3 = (inst >> 12) & 0b111;
        uint32_t funct7 = (inst >> 25) & 0b1111111;

        uinst->rd = (inst >> 7) & 0b11111;
        uinst->rs1 = (inst >> 15) & 0b11111;
        uinst->rs2 = (inst >> 20) & 0b11111;

        switch (funct3)
        {
        case 0x00:
            switch (funct7)
            {
            case 0x00: /* add */
                uinst->inst_id = INST_ADD;
                break;

            case 0x20: /* sub */
                uinst->inst_id = INST_SUB;
                break;

            case 0x01: /* mul */
                uinst->inst_id = INST_MUL;
                break;

            default:
                res = false;
            }
            break;

        case 0x04: 
            switch (funct7)
            {
            case 0x0: /* xor */
                uinst->inst_id = INST_XOR;
                break;

            case 0x01: /* div */
                uinst->inst_id = INST_DIV;
                break;

            default:
                break;
            }
            break;


        case 0x06:
            switch (funct7)
            {
            case 0x0: /* or */
                uinst->inst_id = INST_OR;
                break;
            
            case 0x01: /* rem */
                uinst->inst_id = INST_REM;
                break;

            default:
                break;
            }
            break;

      
        case 0x07: 
            switch (funct7)
            {
            case 0x0: /* and */
                uinst->inst_id = INST_AND;
                break;                
            
            case 0x01: /* remu */
                uinst->inst_id = INST_REMU;
                break;

            case 0x07: /* czero.nez */
                uinst->inst_id = INST_CZERO_NEZ;
                break;

            default:
                break;
            }
            break;


        case 0x01:
            switch (funct7)
            {
            case 0x00: /* sll */
                uinst->inst_id = INST_SLL;
                break;

            case 0x01: /* mulh */
                uinst->inst_id = INST_MULH;
                break;

            default:
                res = false;
            }
            break;

        case 0x05:
            switch (funct7)
            {
            case 0x00: /* srl */
                uinst->inst_id = INST_SRL;
                break;

            case 0x20: /* sra */
                uinst->inst_id = INST_SRA;
                break;

            case 0x01: /* divu */
                uinst->inst_id = INST_DIVU;
                break;

            case 0x07: /* czero.eqz */
                uinst->inst_id = INST_CZERO_EQZ;
                break;

            default:
                res = false;
            }
            break;

        case 0x02:
            switch (funct7)
            {
            case 0x00: /* slt */
                uinst->inst_id = INST_SLT;
                break;

            case 0x01: /* mulhsu */
                uinst->inst_id = INST_MULHSU;
                break;

            default:
                res = false;
            }
            break;

        case 0x03: 
            switch (funct7)
            {
                case 0x00: /* sltu */
                    uinst->inst_id = INST_SLTU;
                    break;

                case 0x01: /* mulhu */
                    uinst->inst_id = INST_MULHU;
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
        uint32_t funct3 = (inst >> 12) & 0b111;

        uinst->rd = (inst >> 7) & 0b11111;
        uinst->rs1 = (inst >> 15) & 0b11111;
        uinst->imm = (inst >> 20) & 0b111111111111;
        uinst->imm = (uinst->imm << 20) >> 20;

        switch (funct3)
        {
        case 0x00:  /* addi */
            uinst->inst_id = INST_ADDI;
            break;

        case 0x04: /* xori */
            uinst->inst_id = INST_XORI;
            break;

        case 0x06: /* ori */
            uinst->inst_id = INST_ORI;
            break;

        case 0x07: /* andi */
            uinst->inst_id = INST_ANDI;
            break;

        case 0x01:
            switch (uinst->imm >> 5)
            {
            case 0x00: /* slli */
                uinst->inst_id = INST_SLLI;
                break;

            default:    
                res = false;
            }
            break;

        case 0x05:
            switch (uinst->imm >> 5)
            {
            case 0x00: /* srli */
                uinst->inst_id = INST_SRLI;
                break;

            case 0x20: /* srai */
                uinst->inst_id = INST_SRAI;
                break;

            default:
                res = false;
            }
            break;

        case 0x02: /* slti */
            uinst->inst_id = INST_SLTI;
            break;

        case 0x03: /* sltiu */
            uinst->inst_id = INST_SLTIU;
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
        uint32_t funct3 = (inst >> 12) & 0b111;

        uinst->rs1 = (inst >> 15) & 0b11111;
        uinst->rs2 = (inst >> 20) & 0b11111;
        uinst->imm = (inst >> 7) & 0b11111;
        uinst->imm |= ((inst >> 25) & 0b1111111) << 5;
        uinst->imm = (uinst->imm << 20) >> 20;
        
        switch (funct3)
        {
        case 0x00: /* sb */
            uinst->inst_id = INST_SB;
            break;

        case 0x01: /* sh */
            uinst->inst_id = INST_SH;
            break;

        case 0x02: /* sw */
            uinst->inst_id = INST_SW;
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
        uint32_t funct3 = (inst >> 12) & 0b111;

        uinst->rd = (inst >> 7) & 0b11111;
        uinst->rs1 = (inst >> 15) & 0b11111;
        uinst->imm = (inst >> 20) & 0b111111111111;
        uinst->imm = (uinst->imm << 20) >> 20;

        switch (funct3)
        {
        case 0x00: /* lb */
            uinst->inst_id = INST_LB;
            break;

        case 0x01: /* lh */
            uinst->inst_id = INST_LH;
            break;

        case 0x02: /* lw */
            uinst->inst_id = INST_LW;
            break;

        case 0x04: /* lbu */
            uinst->inst_id = INST_LBU;
            break;

        case 0x05: /* lhu */
            uinst->inst_id = INST_LHU;
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
        uint32_t funct3 = (inst >> 12) & 0b111;
        
        uinst->rs1 = (inst >> 15) & 0b11111;
        uinst->rs2 = (inst >> 20) & 0b11111;

        uinst->imm = (inst >> 8) & 0b1111;
        uinst->imm |= ((inst >> 7) & 1) << 10;
        uinst->imm |= ((inst >> 25) & 0b111111) << 4;
        uinst->imm = (uinst->imm << 1) | (((inst >> 31) & 1) << 12);
        uinst->imm = (uinst->imm << 19) >> 19;

        switch (funct3)
        {
        case 0x00: /* beq == */
            uinst->inst_id = INST_BEQ;
            break;

        case 0x01: /* bne != */
            uinst->inst_id = INST_BNE;
            break;

        case 0x04: /* blt < */
            uinst->inst_id = INST_BLT;
            break;

        case 0x05: /* bge >= */
            uinst->inst_id = INST_BGE;
            break;

        case 0x06: /* bltu < (u) */
            uinst->inst_id = INST_BLTU;
            break;

        case 0x07: /* bgeu >= (u) */
            uinst->inst_id = INST_BGEU;
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
        uinst->rd = (inst >> 7) & 0b11111;

        uinst->imm = ((inst >> 21) & 0b1111111111) << 1;
        uinst->imm |= ((inst >> 20) & 1) << 11;
        uinst->imm |= ((inst >> 12) & 0b11111111) << 12;
        uinst->imm |= (inst >> 31) << 20;
        uinst->imm = (uinst->imm << 11) >> 11;

        uinst->inst_id = INST_JAL;
    }
    break;

    case 0b1100111: /* jalr */
    {
        uint32_t funct3 = (inst >> 12) & 0b111;

        uinst->rd = (inst >> 7) & 0b11111;
        uinst->rs1 = (inst >> 15) & 0b11111;
        uinst->imm = (inst >> 20) & 0b111111111111;
        uinst->imm = (uinst->imm << 20) >> 20;

        switch (funct3)
        {
        case 0x00:
            uinst->inst_id = INST_JALR;
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
        uinst->rd = (inst >> 7) & 0b11111;
        uinst->imm = (inst >> 12) & 0xfffff;
        uinst->imm = (uinst->imm << 11) >> 11;

        uinst->inst_id = INST_LUI;
    }
    break;

    case 0b0010111: /* auipc */
    {
        uinst->rd = (inst >> 7) & 0b11111;
        uinst->imm = (inst >> 12) & 0xfffff;
        uinst->imm = (uinst->imm << 11) >> 11;
        
        uinst->inst_id = INST_AUIPC;
    }
    break;

    case 0b1110011: /* Env call & breakpoint */
    {
        uint32_t funct12 = (inst >> 20) & 0b111111111111;
        uint32_t funct3 = (inst >> 12) & 0b111;
       
        uinst->rs1 = (inst >> 15) & 0b11111;
        uinst->rd = (inst >> 7) & 0b11111;

        res = (uinst->rs1 == funct3) == (uinst->rd == 0x0);

        switch (funct12)
        {
        case 0x00:
            uinst->inst_id = INST_ECALL;
            break;

        case 0x01:
            uinst->inst_id = INST_BREAK;
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

    return res; 
}


static bool device_run_unpacked_instruction(device_t *dev, uinst_t inst, uint32_t pc_ro)
{
    bool res = true;
    bool pc_updated = false;

    switch(inst.inst_id)
    {
    case INST_NOP:
        break;

    case INST_ADD:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] + dev->regs[inst.rs2]);
        break;

    case INST_SUB:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] - dev->regs[inst.rs2]);
        break;

    case INST_MUL:
        {
            int32_t prod = (int32_t)dev->regs[inst.rs1] * (int32_t)dev->regs[inst.rs2];
            device_set_reg(dev, inst.rd, (uint32_t)prod);
        }
        break;

    case INST_XOR:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] ^ dev->regs[inst.rs2]);
        break;

    case INST_DIV:
        device_set_reg(dev, inst.rd, (uint32_t)((int32_t)dev->regs[inst.rs1] / \
                                           (int32_t)dev->regs[inst.rs2]));
        break;

    case INST_OR:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] | dev->regs[inst.rs2]);
        break;

    case INST_REM:
        device_set_reg(dev, inst.rd, (uint32_t)((int32_t)dev->regs[inst.rs1] % \
                                           (int32_t)dev->regs[inst.rs2]));
        break;

    case INST_AND:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] & dev->regs[inst.rs2]);
        break;

    case INST_REMU:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] % dev->regs[inst.rs2]);
        break;

    case INST_CZERO_NEZ:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs2] ? 0 : dev->regs[inst.rs1]);
        break;

    case INST_SLL:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] << dev->regs[inst.rs2]);
        break;

    case INST_MULH:
        {
            int64_t op1 = (int64_t)(int32_t)dev->regs[inst.rs1];
            int64_t op2 = (int64_t)(int32_t)dev->regs[inst.rs2];
            int64_t prod = op1 * op2;
            // printf("MULH: 0x%08X * 0x%08X = 0x%08X\n", dev->regs[inst.rs1], dev->regs[inst.rs2], (uint32_t)(prod >> 32));
            device_set_reg(dev, inst.rd, (uint32_t)(prod >> 32));
        }
        break;

    case INST_SRL:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] >> dev->regs[inst.rs2]);
        break;

    case INST_SRA:
        device_set_reg(dev, inst.rd, (int32_t)dev->regs[inst.rs1] >> dev->regs[inst.rs2]);
        break;

    case INST_DIVU:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] / dev->regs[inst.rs2]);
        break;

    case INST_CZERO_EQZ:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs2] ? dev->regs[inst.rs1] : 0);
        break;

    case INST_SLT:
        device_set_reg(dev, inst.rd, (int32_t)dev->regs[inst.rs1] < \
                                (int32_t)dev->regs[inst.rs2] ? 1 : 0);
        break;

    case INST_MULHSU:
        {
            int64_t prod = (int64_t)(int32_t)dev->regs[inst.rs1] * (uint64_t)dev->regs[inst.rs2];
            device_set_reg(dev, inst.rd, (uint32_t)(prod >> 32));
        }
        break;

    case INST_SLTU:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] < dev->regs[inst.rs2] ? 1 : 0);
        break;

    case INST_MULHU:
        {
            uint64_t prod = (uint64_t)dev->regs[inst.rs1] * (uint64_t)dev->regs[inst.rs2];
            device_set_reg(dev, inst.rd, (uint32_t)(prod >> 32));
        }
        break;

    case INST_ADDI:
        device_set_reg(dev, inst.rd, (int32_t)dev->regs[inst.rs1] + inst.imm);
        break;

    case INST_XORI:
        device_set_reg(dev, inst.rd, (int32_t)dev->regs[inst.rs1] ^ inst.imm);
        break;

    case INST_ORI:
        device_set_reg(dev, inst.rd, (int32_t)dev->regs[inst.rs1] | inst.imm);
        break;

    case INST_ANDI:
        device_set_reg(dev, inst.rd, (int32_t)dev->regs[inst.rs1] & inst.imm);
        break;

    case INST_SLLI:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] << (inst.imm & 0b11111));
        break;

    case INST_SRLI:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] >> (inst.imm & 0b11111));
        break;

    case INST_SRAI:
        device_set_reg(dev, inst.rd,
                       ((int32_t)(dev->regs[inst.rs1]) >> (inst.imm & 0b11111)));
        break;

    case INST_SLTI:
        device_set_reg(dev, inst.rd, (int32_t)dev->regs[inst.rs1] < inst.imm ? 1 : 0);
        break;

    case INST_SLTIU:
        device_set_reg(dev, inst.rd, dev->regs[inst.rs1] < \
                                ((uint32_t)inst.imm & 0b111111111111) ? 1 : 0);
        break;

    case INST_SB:
        {
            uint32_t addr = dev->regs[inst.rs1] + inst.imm;
            uint8_t bt = dev->regs[inst.rs2] & 0xff;
            res = device_write(dev, addr, &bt, 1);
        }
        break;

    case INST_SH:
        {
            uint32_t addr = dev->regs[inst.rs1] + inst.imm;
            uint16_t hw = dev->regs[inst.rs2] & 0xffff;
            res = device_write(dev, addr, (uint8_t*)&hw, 2);
        }
        break;

    case INST_SW:
        {
            uint32_t addr = dev->regs[inst.rs1] + inst.imm;
            res = device_write(dev, addr, (uint8_t*)&dev->regs[inst.rs2], 4);
        }
        break;

    case INST_LB:
        {
            uint32_t addr = dev->regs[inst.rs1] + inst.imm;
            int8_t sb;
            res = device_read(dev, addr, (uint8_t*)&sb, 1);
            device_set_reg(dev, inst.rd, (int32_t)sb);
        }
        break;

    case INST_LH:
        {
            uint32_t addr = dev->regs[inst.rs1] + inst.imm;
            int16_t shw;
            res = device_read(dev, addr, (uint8_t*)&shw, 2);
            device_set_reg(dev, inst.rd, (int32_t)shw);
        }
        break;

    case INST_LW:
        {
            uint32_t addr = dev->regs[inst.rs1] + inst.imm;
            uint32_t w;
            res = device_read(dev, addr, (uint8_t*)&w, 4);
            device_set_reg(dev, inst.rd, w);
        }
        break;

    case INST_LBU:
        {
            uint32_t addr = dev->regs[inst.rs1] + inst.imm;
            uint8_t ub;
            res = device_read(dev, addr, &ub, 1);
            device_set_reg(dev, inst.rd, ub);
        }
        break;

    case INST_LHU:
        {
            uint32_t addr = dev->regs[inst.rs1] + inst.imm;
            uint16_t hw;
            res = device_read(dev, addr, (uint8_t*)&hw, 2);
            device_set_reg(dev, inst.rd, hw);
        }
        break;

    case INST_BEQ:
        if ((int32_t)(dev->regs[inst.rs1]) == (int32_t)(dev->regs[inst.rs2]))
        {
            dev->pc += inst.imm;
            pc_updated = true;
        }
        break;

    case INST_BNE:
        if ((int32_t)(dev->regs[inst.rs1]) != (int32_t)(dev->regs[inst.rs2]))
        {
            dev->pc += inst.imm;
            pc_updated = true;
        }
        break;

    case INST_BLT:
        if ((int32_t)(dev->regs[inst.rs1]) < (int32_t)(dev->regs[inst.rs2]))
        {
            dev->pc += inst.imm;
            pc_updated = true;
        }
        break;

    case INST_BGE:
        if ((int32_t)(dev->regs[inst.rs1]) >= (int32_t)(dev->regs[inst.rs2]))
        {
            dev->pc += inst.imm;
            pc_updated = true;
        }
        break;

    case INST_BLTU:
        if (dev->regs[inst.rs1] < dev->regs[inst.rs2])
        {
            dev->pc += inst.imm;
            pc_updated = true;
        }
        break;

    case INST_BGEU:
        if (dev->regs[inst.rs1] >= dev->regs[inst.rs2])
        {
            dev->pc += inst.imm;
            pc_updated = true;
        }
        break;

    case INST_JAL:
        device_set_reg(dev, inst.rd, pc_ro + 4);
        dev->pc += inst.imm;
        pc_updated = true;
        break;

    case INST_JALR:
        device_set_reg(dev, inst.rd, pc_ro + 4);
        dev->pc = dev->regs[inst.rs1] + inst.imm;
        pc_updated = true;
        break;

    case INST_LUI:
        device_set_reg(dev, inst.rd, inst.imm << 12);
        break;

    case INST_AUIPC:
        device_set_reg(dev, inst.rd, pc_ro + (inst.imm << 12));
        break;

    case INST_ECALL:
        break;

    case INST_BREAK:
        break;

    case INST_INVALID:
    default:
        res = false;
        break;

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
                if (dev->uinsts && (addr <= dev->prog_end))
                {
                    uint32_t inst_id = (addr - dev->rom.origin) / 4;
                    res = res && device_run_unpacked_instruction(dev, dev->uinsts[inst_id], addr);
                }
                else
                {
                    if (!device_read(dev, addr, (uint8_t*)&inst, sizeof(inst)))
                    {
                        return false;
                    }

                    res = res && device_run_instruction(dev, inst, addr);
                }
            }
        }
#else

        // pthread_barrier_wait(&dev->ilp_barrier1);
        // pthread_barrier_wait(&dev->ilp_barrier2);

#endif
    }
    else
    {
        if (dev->uinsts && (dev->pc <= dev->prog_end))
        {
            uint32_t inst_id = (dev->pc - dev->rom.origin) / 4;
            res = device_run_unpacked_instruction(dev, dev->uinsts[inst_id], dev->pc);
        }
        else
        {
            if (!device_read(dev, dev->pc, (uint8_t*)&inst, sizeof(inst)))
            {
                return false;
            }
            
            res = device_run_instruction(dev, inst, dev->pc);
        }
    }

    return res;
}
