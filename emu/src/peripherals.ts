import type BusInterface from '6502.ts/lib/machine/bus/BusInterface';
import Board from '6502.ts/lib/machine/vanilla/Board';
import type VanillaMemory from '6502.ts/lib/machine/vanilla/Memory';
import vrEmuLcd, { type LcdInstance } from 'vremulcd';

export class MattbrewBoard extends Board {
    bus: MattbrewBus = new MattbrewBus();

    loadRom(romData: Uint8Array): void {
        if (romData.length > 0x2000) {
            throw new Error('ROM data exceeds maximum size of 8KB');
        }
        this.bus.rom = new ReadOnlyMemory(romData, 0xE000, 0x2000);
    }

    protected _createBus() {
        // This may run before MattbrewBoard's constructor, so we need to build bus here.
        this.bus = new MattbrewBus();
        return this.bus as unknown as VanillaMemory;
    }
}

export class MattbrewBus implements BusInterface {
    // 0x0000 - 0x7FFF
    main_memory: Memory = Memory.withSize(0x0000, 0x8000);
    // 0x8000 - 0xBFFF
    // banked_memory
    // 0xC000 - 0xC7FF
    via: ViaLcd = new ViaLcd(16, 2);
    // 0xC800 - 0xCFFF
    // RPI
    // 0xD000 - 0xD7FF
    // Ram bank register
    // 0xD800 - 0xDFFF
    // unmapped
    // 0xE000 - 0xFFFF
    rom: ReadOnlyMemory = new ReadOnlyMemory(new Uint8Array(0x2000), 0xE000, 0x2000);

    read(address: number): number {
        return this.chipSelect(address).read(address);
    }
    peek(address: number): number {
        return this.chipSelect(address).peek(address);
    }
    readWord(address: number): number {
        const lowByte = this.chipSelect(address).read(address);
        const highByte = this.chipSelect((address + 1) & 0xffff).read((address + 1) & 0xffff);
        return lowByte + (highByte << 8);
    }
    write(address: number, value: number): void {
        this.chipSelect(address).write(address, value);
    }
    poke(address: number, value: number): void {
        this.chipSelect(address).poke(address, value);
    }

    reset(): void {
        this.main_memory.reset();
        this.rom.reset();
        this.via.reset();
    }

    clear(): void {
        this.main_memory.clear();
        this.rom.clear();
    }

    protected chipSelect(address: number): BusInterface {
        if (address < 0x8000) {
            return this.main_memory;
        } else if (address >= 0xC000 && address < 0xC800) {
            return this.via;
        } else if (address >= 0xE000) {
            return this.rom;
        } else {
            throw new Error(`Address ${address.toString(16)} is not mapped to any device`);
        }
    }
}

// 6522 VIA + HD44780 LCD peripheral.
//
// VIA register map (address & 0x0F, mirrored through 0xC000-0xC7FF):
//   0: ORB/IRB – Port B output/input  (LCD DB0–DB7)
//   1: ORA/IRA – Port A output/input  (bit5=RS, bit6=R/W#, bit7=E)
//   2: DDRB    – Port B data direction (1=output)
//   3: DDRA    – Port A data direction
//   4-15: unimplemented (reads 0xFF)
//
// Hardware connections:
//   PB0–PB7 → LCD DB0–DB7
//   PA5     → RS   (0=command, 1=data)
//   PA6     → R/W# (0=write,   1=read)
//   PA7     → E    (latch on falling edge)
export class ViaLcd implements BusInterface {
    private orb = 0;
    private ora = 0;
    private ddrb = 0;
    private ddra = 0;
    private lcd: LcdInstance | null = null;

    constructor(cols: number, rows: number) {
        vrEmuLcd.setLoadedCallback(() => {
            this.lcd = vrEmuLcd.newLCD(cols, rows, vrEmuLcd.CharacterRom.European);
        });
    }

    getLcd(): LcdInstance | null {
        return this.lcd;
    }

    read(address: number): number {
        return this.peek(address);
    }

    peek(address: number): number {
        switch ((address - 0xC000) & 0x0F) {
            case 0: return this.orb;
            case 1: return this.ora;
            case 2: return this.ddrb;
            case 3: return this.ddra;
            default: return 0xFF;
        }
    }

    readWord(address: number): number {
        return this.read(address) | (this.read((address + 1) & 0xFFFF) << 8);
    }

    write(address: number, value: number): void {
        switch ((address - 0xC000) & 0x0F) {
            case 0: this.orb = value & 0xFF; break;
            case 1: this.writeOra(value & 0xFF); break;
            case 2: this.ddrb = value & 0xFF; break;
            case 3: this.ddra = value & 0xFF; break;
            // other registers are not implemented
        }
    }

    poke(address: number, value: number): void {
        this.write(address, value);
    }

    reset(): void {
        this.orb = 0;
        this.ora = 0;
        this.ddrb = 0;
        this.ddra = 0;
    }

    private writeOra(value: number): void {
        const prevE = (this.ora >> 7) & 1;
        this.ora = value;
        // Latch to LCD on falling edge of E (bit 7)
        if (prevE === 1 && ((value >> 7) & 1) === 0) {
            this.latchToLcd();
        }
    }

    private latchToLcd(): void {
        if (!this.lcd) return;

        const rs = (this.ora >> 5) & 1;   // PA5: RS
        const rw = (this.ora >> 6) & 1;   // PA6: R/W#

        // DDR validation – warn if control lines or data bus aren't configured correctly
        if ((this.ddra & 0xE0) !== 0xE0) {
            console.warn('VIA: DDRA PA5/6/7 must be outputs for LCD access (DDRA=0x' +
                this.ddra.toString(16) + ')');
        }
        if (rw === 0 && this.ddrb !== 0xFF) {
            console.warn('VIA: DDRB must be 0xFF (all outputs) for LCD write (DDRB=0x' +
                this.ddrb.toString(16) + ')');
        }
        if (rw === 1 && this.ddrb !== 0x00) {
            console.warn('VIA: DDRB must be 0x00 (all inputs) for LCD read (DDRB=0x' +
                this.ddrb.toString(16) + ')');
        }

        if (rw === 0) {
            // Write to LCD
            if (rs === 0) {
                this.lcd.sendCommand(this.orb);   // RS=0: instruction register
            } else {
                this.lcd.writeByte(this.orb);     // RS=1: data register
            }
        } else {
            // Read from LCD – result goes back on Port B
            if (rs === 0) {
                // Read busy flag + address counter (BF always 0 in emulation)
                this.orb = this.lcd.readAddress() & 0x7F;
            } else {
                this.orb = this.lcd.readByte();
            }
        }
    }
}

export class Memory implements BusInterface {
    private baseAddress: number;
    private data: Uint8Array;

    static withSize(baseAddress: number, size: number): Memory {
        return new Memory(new Uint8Array(size), baseAddress, size);
    }

    constructor(data: Uint8Array, baseAddress: number, size: number) {
        this.baseAddress = baseAddress;
        this.data = new Uint8Array(size);
        this.data.set(data.subarray(0, size));
    }

    read(address: number): number {
        return this.data[address - this.baseAddress];
    }

    peek(address: number): number {
        return this.data[address - this.baseAddress];
    }

    readWord(address: number): number {
        const relativeAddress = address - this.baseAddress;
        return this.data[relativeAddress] + (this.data[(relativeAddress + 1) & 0xffff] << 8);
    }

    write(address: number, value: number): void {
        this.data[address - this.baseAddress] = value;
    }

    poke(address: number, value: number): void {
        this.data[address - this.baseAddress] = value;
    }

    reset(): void {}

    clear(): void {
        this.data.fill(0);
    }
}

export class ReadOnlyMemory extends Memory {
    write(_address: number, _value: number): void {
        throw new Error('Cannot write to read-only memory');
    }

    clear(): void {}
}
