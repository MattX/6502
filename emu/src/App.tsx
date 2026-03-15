import "./App.css";
import { useState, useEffect, useRef, useCallback } from "react";
import init, { Emulator } from "../emu-core/pkg/emu_core.js";

function hex8(n: number): string {
  return n.toString(16).padStart(2, "0").toUpperCase();
}

function hex16(n: number): string {
  return n.toString(16).padStart(4, "0").toUpperCase();
}

function App() {
  const [emu, setEmu] = useState<Emulator | null>(null);
  const [error, setError] = useState<string | null>(null);

  // The `cancelled` guard is critical here: React StrictMode double-fires effects,
  // causing two separate WASM instances to be loaded (the `init()` idempotency check
  // races). Emulators created on different instances live on different WASM heaps, so
  // when GC frees the orphaned one via FinalizationRegistry, it calls `__wbg_emulator_free`
  // with a pointer from heap A on heap B — a cross-heap free that corrupts dlmalloc.
  useEffect(() => {
    let cancelled = false;
    init().then(() => {
      if (!cancelled) setEmu(new Emulator());
    }).catch((e) => {
      if (!cancelled) setError(String(e));
    });
    return () => { cancelled = true; };
  }, []);

  if (error) return <div className="app">Failed to load WASM: {error}</div>;
  if (!emu) return <div className="app">Loading…</div>;

  return <EmulatorUI emu={emu} />;
}

function EmulatorUI({ emu }: { emu: Emulator }) {
  const [running, setRunning] = useState(false);
  const [, setTick] = useState(0);
  const [cyclesPerSec, setCyclesPerSec] = useState<number | null>(null);
  const intervalRef = useRef<number | null>(null);
  const perfRef = useRef({ lastTime: 0, lastCycles: 0 });

  const terminalRef = useRef<HTMLPreElement>(null);
  const [lcdPixels, setLcdPixels] = useState<Uint8Array | null>(null);

  const refresh = useCallback(() => {
    setLcdPixels(emu.lcd_pixels(Date.now()));
    setTick((t) => t + 1);
  }, [emu]);

  useEffect(() => {
    if (running) {
      perfRef.current = { lastTime: performance.now(), lastCycles: emu.cycles() };
      intervalRef.current = window.setInterval(() => {
        emu.run_for_cycles(100_000);
        const now = performance.now();
        const elapsed = now - perfRef.current.lastTime;
        if (elapsed >= 1000) {
          const dc = emu.cycles() - perfRef.current.lastCycles;
          setCyclesPerSec(dc / (elapsed / 1000));
          perfRef.current = { lastTime: now, lastCycles: emu.cycles() };
        }
        refresh();
      }, 100);
    }
    return () => {
      if (intervalRef.current !== null) {
        clearInterval(intervalRef.current);
        intervalRef.current = null;
      }
    };
  }, [running, emu, refresh]);

  function start() { setRunning(true); }
  function stop() { setRunning(false); setCyclesPerSec(null); }

  function cycle() {
    emu.step();
    refresh();
  }

  function reset() {
    setRunning(false);
    emu.reset();
    refresh();
  }

  function handleRomUpload(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (ev) => {
      const data = new Uint8Array(ev.target!.result as ArrayBuffer);
      emu.load_rom(data);
      refresh();
    };
    reader.readAsArrayBuffer(file);
    e.target.value = "";
  }

  const pc = emu.pc();
  const sp = emu.sp();
  const disasm = emu.disassemble_at(pc, 20);

  return (
    <div className="app">
      <div className="controls">
        <button onClick={reset}>Reset</button>
        {running ? (
          <button onClick={stop}>Stop</button>
        ) : (
          <>
            <button onClick={start}>Start</button>
            <button onClick={cycle}>Cycle</button>
          </>
        )}
        <label className="rom-upload-btn">
          Upload ROM
          <input type="file" accept=".bin,.rom" onChange={handleRomUpload} />
        </label>
        {cyclesPerSec !== null && (
          <span className="perf-counter">
            {cyclesPerSec >= 1_000_000
              ? (cyclesPerSec / 1_000_000).toFixed(2) + " MHz"
              : cyclesPerSec >= 1_000
                ? (cyclesPerSec / 1_000).toFixed(1) + " kHz"
                : Math.round(cyclesPerSec) + " Hz"}
          </span>
        )}
      </div>
      <div className="main-layout">
        <CpuWidget
          pc={pc} sp={sp}
          a={emu.a()} x={emu.x()} y={emu.y()}
          status={emu.status()}
          disasm={disasm}
        />
        <LcdWidget emu={emu} pixels={lcdPixels} />
        <TerminalWidget
          text={emu.terminal_text()}
          terminalRef={terminalRef}
          onKey={(data) => emu.send_keyboard_input(data)}
        />
        <div className="memory-panels">
          <PageBrowser emu={emu} sp={sp} />
          <StackDisplay emu={emu} sp={sp} />
        </div>
      </div>
    </div>
  );
}

function CpuWidget({ pc, sp, a, x, y, status, disasm }: {
  pc: number; sp: number; a: number; x: number; y: number;
  status: number; disasm: string;
}) {
  const lines = disasm.split("\n");
  const marked = lines
    .map((line, i) => (i === 0 ? "> " + line : "  " + line))
    .join("\n");

  return (
    <div className="cpu-widget">
      <h2>CPU</h2>
      <table className="registers">
        <tbody>
          <tr><th>PC</th><td>${hex16(pc)}</td></tr>
          <tr><th>SP</th><td>${hex8(sp)}</td></tr>
          <tr><th>A</th><td>${hex8(a)}</td></tr>
          <tr><th>X</th><td>${hex8(x)}</td></tr>
          <tr><th>Y</th><td>${hex8(y)}</td></tr>
          <tr><th>P</th><td>${hex8(status)}</td></tr>
        </tbody>
      </table>
      <h3>Disassembly</h3>
      <pre className="disassembly">{marked}</pre>
    </div>
  );
}

function PageDisplay({ data, baseAddress, marker }: {
  data: Uint8Array; baseAddress: number; marker?: number;
}) {
  const bytesPerRow = 16;
  const rows = [];
  for (let i = 0; i < 16; i++) {
    const rowAddress = baseAddress + i * bytesPerRow;
    const markerRow = marker !== undefined && Math.floor(marker / bytesPerRow) === i;
    const markerCol = marker !== undefined ? marker % bytesPerRow : -1;
    const cells = [];
    for (let j = 0; j < bytesPerRow; j++) {
      const byte = hex8(data[i * bytesPerRow + j]);
      cells.push(
        <td key={j} className={markerRow && j === markerCol ? "marker-cell" : undefined}>
          {byte}
        </td>
      );
    }
    rows.push(
      <tr key={i} className={markerRow ? "marker-row" : undefined}>
        <td className="addr">{hex16(rowAddress)}</td>
        {cells}
      </tr>
    );
  }

  return (
    <table>
      <thead>
        <tr>
          <th>Addr</th>
          {Array.from({ length: 16 }, (_, i) => (
            <th key={i}>+{i.toString(16).toUpperCase()}</th>
          ))}
        </tr>
      </thead>
      <tbody>{rows}</tbody>
    </table>
  );
}

function PageBrowser({ emu, sp }: { emu: Emulator; sp: number }) {
  const [page, setPage] = useState(0);
  const [inputVal, setInputVal] = useState("00");

  function handleInput(e: React.ChangeEvent<HTMLInputElement>) {
    const raw = e.target.value.toUpperCase().replace(/[^0-9A-F]/g, "").slice(0, 2);
    setInputVal(raw);
    const v = parseInt(raw, 16);
    if (!isNaN(v)) setPage(Math.max(0, Math.min(255, v)));
  }

  function handleBlur() {
    setInputVal(page.toString(16).padStart(2, "0").toUpperCase());
  }

  const data = emu.read_page(page);

  return (
    <div>
      <h3>
        Memory – page{" "}
        <input
          className="page-input"
          value={inputVal}
          onChange={handleInput}
          onBlur={handleBlur}
          spellCheck={false}
        />
      </h3>
      <PageDisplay
        data={data}
        baseAddress={page * 0x100}
        marker={page === 0x01 ? sp : undefined}
      />
    </div>
  );
}

function TerminalWidget({ text, terminalRef, onKey }: {
  text: string;
  terminalRef: React.RefObject<HTMLPreElement | null>;
  onKey: (data: Uint8Array) => void;
}) {
  useEffect(() => {
    if (terminalRef.current) {
      terminalRef.current.scrollTop = terminalRef.current.scrollHeight;
    }
  }, [text, terminalRef]);

  function handleKeyDown(e: React.KeyboardEvent) {
    e.preventDefault();
    let bytes: number[] | null = null;
    if (e.key.length === 1) {
      bytes = [...new TextEncoder().encode(e.key)];
    } else {
      switch (e.key) {
        case "Enter": bytes = [0x0A]; break;
        case "Backspace": bytes = [0x08]; break;
        case "Escape": bytes = [0x1B]; break;
        case "ArrowUp": bytes = [0x1B, 0x5B, 0x41]; break;
        case "ArrowDown": bytes = [0x1B, 0x5B, 0x42]; break;
        case "ArrowRight": bytes = [0x1B, 0x5B, 0x43]; break;
        case "ArrowLeft": bytes = [0x1B, 0x5B, 0x44]; break;
        case "Tab": bytes = [0x09]; break;
        case "Delete": bytes = [0x1B, 0x5B, 0x33, 0x7E]; break;
      }
    }
    if (bytes) {
      onKey(new Uint8Array(bytes));
    }
  }

  return (
    <div className="terminal-widget">
      <h2>Terminal</h2>
      <pre
        ref={terminalRef}
        className="terminal-output"
        tabIndex={0}
        onKeyDown={handleKeyDown}
      >
        {text || "\u00A0"}
      </pre>
    </div>
  );
}

const LCD_SCALE = 3;
const LCD_COLOR_ON = [0x33, 0xFF, 0x33];
const LCD_COLOR_OFF = [0x0A, 0x2A, 0x0A];
const LCD_COLOR_BG = [0x05, 0x15, 0x05];

function LcdWidget({ emu, pixels }: { emu: Emulator; pixels: Uint8Array | null }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const w = emu.lcd_width();
  const h = emu.lcd_height();

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !pixels) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const img = ctx.createImageData(w, h);
    for (let i = 0; i < pixels.length; i++) {
      const color =
        pixels[i] === 1 ? LCD_COLOR_ON :
        pixels[i] === 0 ? LCD_COLOR_OFF :
        LCD_COLOR_BG;
      img.data[i * 4] = color[0];
      img.data[i * 4 + 1] = color[1];
      img.data[i * 4 + 2] = color[2];
      img.data[i * 4 + 3] = 255;
    }
    ctx.putImageData(img, 0, 0);
  }, [pixels, w, h]);

  return (
    <div className="lcd-widget">
      <h2>LCD</h2>
      <canvas
        ref={canvasRef}
        width={w}
        height={h}
        style={{ width: w * LCD_SCALE, height: h * LCD_SCALE }}
      />
    </div>
  );
}

function StackDisplay({ emu, sp }: { emu: Emulator; sp: number }) {
  const data = emu.read_page(1);
  return (
    <div>
      <h3>Stack (SP=${hex8(sp)})</h3>
      <PageDisplay data={data} baseAddress={0x0100} marker={sp} />
    </div>
  );
}

export default App;
