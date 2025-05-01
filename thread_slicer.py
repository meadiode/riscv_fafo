
from struct import unpack, pack
from dataclasses import dataclass
import sys

@dataclass
class ElfHdr:
    magic         : bytes
    bitness       : int
    data          : int
    id_version    : int
    os_abi        : int
    abi_ver       : int
    pad           : bytes

    type_         : int
    machine       : int
    version       : int
    entry         : int
    phoff         : int
    shoff         : int
    flags         : int
    ehsize        : int
    phentsize     : int
    phnum         : int
    shentsize     : int
    shnum         : int
    shstrndx      : int


@dataclass
class SecHdr:
    name          : int
    type_         : int
    flags         : int
    addr          : int
    offset        : int
    size          : int
    link          : int
    info          : int
    addralign     : int
    entsize       : int

ELF_HDR_SIZE = 52
SEC_HDR_SIZE = 40

def twocomp(val, n_bits):
    if val & (1 << (n_bits - 1)):
        return val - (1 << n_bits)
    return val


class MLoc:

    def __init__(self, reg, offset):
        self.val = (reg, offset)


    def __hash__(self):
        return 42


    def __eq__(self, other):
        if not isinstance(other, MLoc):
            return False 
        if self.val[0] == other.val[0]:
            return self.val[1] == other.val[1]
        else:
            return True


PC_REG = 32

class Program:

    rom_start = 0x08000000
    rom_size = 1024 * 1024 * 10
    prog_end = 0x08000000
    max_slice_len = 8

    def __init__(self, elf_file_name):
        self.rom = bytearray(self.rom_size)
        self.basic_blocks = {}

        self.load_from_elf(elf_file_name)
        self.find_all_basic_blocks()
        self.slice_all_basic_blocks()


    def load_from_elf(self, elf_file_name):
        elf = open(elf_file_name, 'rb')
        hdr_bytes = elf.read(ELF_HDR_SIZE)
        hdr = ElfHdr(*unpack('4sBBBBB7sHHIIIIIHHHHHH', hdr_bytes))

        elf.seek(hdr.shoff)
        sectable = []

        for ns in range(hdr.shnum):
            sec_hdr_bytes = elf.read(SEC_HDR_SIZE)
            sec_hdr = SecHdr(*unpack('IIIIIIIIII', sec_hdr_bytes))
                
            # if PROGBITS
            if sec_hdr.type_ == 1 and \
                    (0 <= (sec_hdr.addr - self.rom_start) <= self.rom_size):
                sectable.append(sec_hdr)

        for sec_hdr in sectable:
            if sec_hdr.flags & 0x04: # if executable
                sec_end = sec_hdr.addr + sec_hdr.size
                if sec_end > self.prog_end:
                    self.prog_end = sec_end

            elf.seek(sec_hdr.offset)
            sec_data = elf.read(sec_hdr.size)
            offset = sec_hdr.addr - self.rom_start
            self.rom[offset:offset + sec_hdr.size] = sec_data
        elf.close()


    def find_basic_block(self, pc):
        '''
        Determine a block of consequent instructions starting from pc with 
        no branching ops, i.e. a basic block. JAL is not considered a branching
        operation, while JALR is.

        Return: a list of instructions and its dependencies of a basic block,
                and a set of starting locations of adjacent blocks.
        '''
        start = pc
        bblock = []
        heads = set()
        locs = set()

        while True:
            reads, writes, branch, next_pc = self.decode_inst(pc)
            if next_pc in locs:
                break
            bblock.append((pc, reads, writes))
            locs.add(pc)
            
            if ((next_pc - pc) != 4) or branch:
                heads.add(pc + 4)
                heads.add(next_pc)
            if branch:
                break
            pc = next_pc

        heads_ = set([h for h in heads if h < self.prog_end])

        return bblock, heads_


    def find_all_basic_blocks(self):
        '''
        Find all basic blocks and store them into the basic_blocks dict.
        '''
        for pc in range(self.rom_start,  self.prog_end, 4):
            if not (pc in self.basic_blocks):
                bb, hd = self.find_basic_block(pc)
                self.basic_blocks[bb[0][0]] = bb


    def slice_basic_block(self, bb_id):
        '''
        Determine which instructions in the basic block at address bb_id
        could be executed in parallel, i.e. slice the block.
        Return: a list of tuples, where each tuple contains addresses of
        instructions that could be safely executed in parallel together.
        '''
        bb = self.basic_blocks[bb_id]

        slices = []

        for inst, rd, wd in bb[:-1]:
            slice_id = len(slices)
            
            for sinsts, srd, swd in slices[::-1]:
                if set(wd).intersection(srd) or \
                   set(rd).intersection(swd) or \
                   set(wd).intersection(swd):
                    break
                slice_id -= 1

            if (slice_id + 1) > len(slices):
                slices.append(([], set(), set()))

            free_space = False
            for i in range(slice_id, len(slices)):
                if (self.max_slice_len is None) or \
                        (len(slices[i][0]) < self.max_slice_len):
                    slice_id = i
                    free_space = True
                    break
            
            if not free_space:
                slices.append(([], set(), set()))
                slice_id = len(slices) - 1
            
            slices[slice_id][0].append(inst)
            slices[slice_id][1].update(rd)
            slices[slice_id][2].update(wd)

        res = []
        for sl in slices:
            res.append(tuple(sl[0]))

        res.append((bb[-1][0],))

        return res


    def slice_all_basic_blocks(self):
        '''
        Slice all the basic blocks and store the result in the
        sliced_blocks dict. Also print some stats.
        '''
        self.sliced_blocks = {}
        stat_max_slice_len = 0
        stat_total_slice_len = 0
        stat_num_slices = 0

        for bb in self.basic_blocks:
            sliced = self.slice_basic_block(bb)

            if sliced:
                for sl in sliced:
                    if len(sl) > stat_max_slice_len:
                        stat_max_slice_len = len(sl)
                    stat_num_slices += 1
                    stat_total_slice_len += len(sl)

                self.sliced_blocks[bb] = sliced

        self.stat_max_slice_len = stat_max_slice_len
        avg_len = stat_total_slice_len / stat_num_slices
        print(f'Max slice length:        {stat_max_slice_len}')
        print(f'Avg slice length:        {avg_len:.02f}')
        print(f'Number of sliced blocks: {len(self.sliced_blocks)}')

        num_cycles = 0
        num_idle_cycles = 0
        for sb in self.sliced_blocks.values():
            for sl in sb:
                num_cycles += stat_max_slice_len
                num_idle_cycles += stat_max_slice_len - len(sl)
        idle_prc = (num_idle_cycles / num_cycles) * 100.0
        print(f'Idle cycles:             {idle_prc:.02f}%')


    def get_inst(self, pc):
        if pc == 0:
            return 0

        offset = pc - self.rom_start

        if offset >= self.rom_size:
            raise ValueError(f'Invalid program address: 0x{pc:08X}')
        inst = unpack('I', self.rom[offset: offset + 4])[0]

        return inst


    def decode_inst(self, pc):
        '''
        Decode a single instruction at address pc and determine its
        dependencies and whether it is a branching instruction or not.
        Return: a tuple of read registers/memmory,
                a tuple of written registers/memory,
                True/False if branching,
                address of the next instruction
        '''
        inst = self.get_inst(pc)

        opcode = inst & 0b1111111
        pc_updated = False
        branch = False
        writes = ()
        reads = ()

        match opcode:
            
            case 0b0110011: # Integer Register-Register ops
                rd = (inst >> 7) & 0b11111
                rs1 = (inst >> 15) & 0b11111
                rs2 = (inst >> 20) & 0b11111
                writes = (rd,)
                reads = (rs1, rs2)

            case 0b0010011: # Integer Register-Immediate ops
                rd = (inst >> 7) & 0b11111;
                rs1 = (inst >> 15) & 0b11111;
                writes = (rd,)
                reads = (rs1,)

            case 0b0100011: # Store ops
                imm = (inst >> 7) & 0b11111
                funct3 = (inst >> 12) & 0b111
                rs1 = (inst >> 15) & 0b11111
                rs2 = (inst >> 20) & 0b11111
                imm |= ((inst >> 25) & 0b1111111) << 5
                imm = twocomp(imm, 12)
                
                match funct3:
                    
                    case 0x00:
                        writes = (MLoc(rs1, imm),)
                    
                    case 0x01:
                        writes = (MLoc(rs1, imm), MLoc(rs1, imm + 1))
                    
                    case 0x02:
                        writes = (MLoc(rs1, imm), MLoc(rs1, imm + 1),
                                  MLoc(rs1, imm + 2), MLoc(rs1, imm + 3))
                    case _:
                        raise ValueError(f'Invalid store operation: 0x{inst:08X}')

                reads = (rs1, rs2)

            case 0b0000011: # Load ops
                rd = (inst >> 7) & 0b11111
                funct3 = (inst >> 12) & 0b111
                rs1 = (inst >> 15) & 0b11111
                imm = (inst >> 20) & 0b111111111111
                imm = twocomp(imm, 12)

                match funct3:
                    
                    case 0x00 | 0x04:
                        reads = (rs1, MLoc(rs1, imm))

                    case 0x01 | 0x05:
                        reads = (rs1, MLoc(rs1, imm), MLoc(rs1, imm + 1))

                    case 0x02:
                        reads = (rs1, MLoc(rs1, imm), MLoc(rs1, imm + 1),
                                 MLoc(rs1, imm + 2), MLoc(rs1, imm + 3))

                    case _:
                        raise ValueError(f'Invalid load operation: 0x{inst:08X}')

                writes = (rd,)


            case 0b1100011: # Branching ops
                imm = (inst >> 8) & 0b1111
                imm |= ((inst >> 7) & 1) << 10
                rs1 = (inst >> 15) & 0b11111
                rs2 = (inst >> 20) & 0b11111
                imm |= ((inst >> 25) & 0b111111) << 4
                imm = (imm << 1) | (((inst >> 31) & 1) << 12)
                imm = twocomp(imm, 13)

                branch = True
                pc_updated = True
                writes = (PC_REG,)
                reads = (rs1, rs2)
                pc += imm

            case 0b1101111: # jal
                rd = (inst >> 7) & 0b11111
                imm = ((inst >> 21) & 0b1111111111) << 1
                imm |= ((inst >> 20) & 1) << 11
                imm |= ((inst >> 12) & 0b11111111) << 12
                imm |= (inst >> 31) << 20
                imm = twocomp(imm, 21)

                pc_updated = True
                reads = (PC_REG,)
                writes = (PC_REG, rd)
                pc += imm

            case 0b1100111: # jalr
                rd = (inst >> 7) & 0b11111
                funct3 = (inst >> 12) & 0b111
                rs1 = (inst >> 15) & 0b11111

                pc_updated = True
                branch = True
                reads = (PC_REG, rs1)
                writes = (PC_REG, rd)

            case 0b0110111: # lui
                rd = (inst >> 7) & 0b11111
                writes = (rd,)

            case 0b0010111: # auipc
                rd = (inst >> 7) & 0b11111
                reads = (PC_REG,)
                writes = (rd,)

            case 0b1110011: # Env call & breakpoint
                # ignored for now
                pass

            case _:
                raise ValueError(f'Unrecognized opcode: 0x{opcode:02X} at 0x{pc:08X}')

        reads = tuple([n for n in reads if n != 0])
        writes = tuple([n for n in writes if n != 0])

        if not pc_updated:
            pc += 4

        return reads, writes, branch, pc


    def dump_to_txt(self, file_name, idle_cycles=False):
        file = open(file_name, 'w')

        file.write(f'num blocks: {len(self.sliced_blocks)}\n')
        file.write(f'num threads: {self.stat_max_slice_len}\n\n')

        ids = list(self.sliced_blocks.keys())
        ids.sort()

        for pc in ids:
            sb = self.sliced_blocks[pc]
            file.write(f'0x{sb[0][0]:08X}:\n')

            for sl in sb:
                if idle_cycles:
                    sl = sl + (0,) * (self.stat_max_slice_len - len(sl))
                file.write('    ')
                for addr in sl:
                    file.write(f'0x{addr:08X} ')
                file.write('\n')
            file.write('\n')

        file.close()


    def dump_to_ilp(self, file_name):
        file = open(file_name, 'wb')

        file.write(pack('4s', bytes('ILP', 'utf-8')))
        file.write(pack('I', len(self.sliced_blocks)))
        file.write(pack('I', self.stat_max_slice_len))
        ids = list(self.sliced_blocks.keys())
        ids.sort()

        offset = 0
        for pc in ids:
            sb = self.sliced_blocks[pc]
            size = 0
            for sl in sb:
                size += (len(sl) + 1) * 4
            file.write(pack('III', pc, offset, size))
            offset += size

        for pc in ids:
            sb = self.sliced_blocks[pc]
            for sl in sb:
                sl = sl + (0,)
                file.write(pack('I' * len(sl), *sl))

        file.close()


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise RuntimeError('Usage: <input .elf file> <output .ilp file>')
    else:
        prog = Program(sys.argv[1])
        prog.dump_to_ilp(sys.argv[2])
        prog.dump_to_txt(sys.argv[2] + '.txt')
