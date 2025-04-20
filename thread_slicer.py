
from struct import unpack
from dataclasses import dataclass

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


PC_REG = 32

class Program:

    rom_start = 0x08000000
    rom_size = 1024 * 1024 * 10
    prog_end = 0x08000000

    def __init__(self, elf_file_name):
        self.rom = bytearray(self.rom_size)
        self.basic_blocks = {}

        self.load_from_elf(elf_file_name)
        self.find_all_basic_blocks()

        # lns = [len(bb) for bb in self.basic_blocks.values()]
        # lns.sort()
        # print(lns)
        # print(sum(lns) / len(lns))
        
        # from collections import Counter
        # ctr = Counter(lns)
        # print(ctr)


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

            print(f'0x{pc:08X}:', reads, writes, branch)
            
            if ((next_pc - pc) != 4) or branch:
                heads.add(pc + 4)
                heads.add(next_pc)
            if branch:
                break
            pc = next_pc

        heads_ = set([h for h in heads if h < self.prog_end])

        print('')
        self.basic_blocks[start] = bblock
        return bblock, heads_


    def find_all_basic_blocks(self):
        '''
        Find all basic blocks and store them into the basic_blocks dict.
        '''
        heads = {self.rom_start,}

        while heads:
            n_heads = set()
            
            for pc in heads:
                if not (pc in self.basic_blocks):
                    bb, hd = self.find_basic_block(pc)
                    n_heads.update(hd)
            heads = n_heads.copy()


    def decode_inst(self, pc):
        '''
        Decode a single instruction at address pc and determine its
        dependencies and whether it is a branching instruction or not.
        Return: a tuple of read registers/memmory,
                a tuple of written registers/memory,
                True/False if branching,
                address of the next instruction
        '''
        offset = pc - self.rom_start

        if offset >= self.rom_size:
            raise ValueError(f'Invalid program address: 0x{pc:08X}')

        inst = unpack('I', self.rom[offset: offset + 4])[0]

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
                        writes = ((rs1, imm),)
                    
                    case 0x01:
                        writes = ((rs1, imm), (rs1, imm + 1))
                    
                    case 0x02:
                        writes = ((rs1, imm), (rs1, imm + 1),
                                  (rs1, imm + 2), (rs1, imm + 3))
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
                        reads = ((rs1, imm),)

                    case 0x01 | 0x05:
                        reads = ((rs1, imm), (rs1, imm + 1))

                    case 0x02:
                        reads = ((rs1, imm), (rs1, imm + 1),
                                 (rs1, imm + 2), (rs1, imm + 3))

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
                imm = (inst >> 20) & 0b111111111111
                imm = twocomp(imm, 11)

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
                pass

            case _:
                raise ValueError(f'Unrecognized opcode: 0x{opcode:02X} at 0x{pc:08X}')

        reads = tuple([n for n in reads if n != 0])
        writes = tuple([n for n in writes if n != 0])

        if not pc_updated:
            pc += 4

        return reads, writes, branch, pc



if __name__ == '__main__':
    prog = Program('./build/prog05.elf')
    # prog = Program('./doomgeneric/doomgeneric/doomrv.elf')
