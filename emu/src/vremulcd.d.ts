declare module "vremulcd" {
  interface ColorScheme {
    BackColor: string;
    PixelOnColor: string;
    PixelOffColor: string;
  }

  export interface LcdInstance {
    sendCommand(cmd: number): void;
    writeByte(data: number): void;
    writeString(str: string): void;
    readByte(): number;
    readAddress(): number;
    updatePixels(): void;
    numPixelsX: number;
    numPixelsY: number;
    pixelState(x: number, y: number): -1 | 0 | 1;
    colorScheme: ColorScheme;
    render(
      ctx: CanvasRenderingContext2D,
      x: number,
      y: number,
      w: number,
      h: number
    ): void;
    destroy(): void;
  }

  interface VrEmuLcd {
    newLCD(w: number, h: number, rom?: number): LcdInstance;
    setLoadedCallback(cb: () => void): void;
    CharacterRom: { A00: 0; Japanese: 0; A02: 1; European: 1 };
    Commands: Record<string, number>;
    Schemes: Record<string, ColorScheme>;
  }

  const vrEmuLcd: VrEmuLcd;
  export default vrEmuLcd;
}
