import type CpuInterface from "6502.ts/lib/machine/cpu/CpuInterface";
import "./App.css";
import { MattbrewBoard, type ViaLcd } from "./peripherals";
import { useState, useRef, useEffect } from "react";
import PeriodicScheduler from "6502.ts/lib/tools/scheduler/PeriodicScheduler";
import Debugger from "6502.ts/lib/machine/Debugger";
import type BusInterface from "6502.ts/lib/machine/bus/BusInterface";

const mattbrewBoard = new MattbrewBoard();
mattbrewBoard.boot();
const scheduler = new PeriodicScheduler(/*period in ms*/ 100);
const dbg = new Debugger();
dbg.attach(mattbrewBoard);

function App() {
  const [running, setRunning] = useState<boolean>(mattbrewBoard.getTimer().isRunning());
  const [_, setTick] = useState<number>(0);

  function refresh() {
    setTick((tick) => tick + 1);
  }

  if (running) {
    setTimeout(refresh, 100);
  }

  function start() {
    mattbrewBoard.getTimer().start(scheduler);
    setRunning(true);
  }

  function stop() {
    mattbrewBoard.getTimer().stop();
    setRunning(false);
  }

  function cycle() {
    mattbrewBoard.getCpu().cycle();
    refresh();
  }

  function reset() {
    mattbrewBoard.reset(true);
    mattbrewBoard.boot();
    refresh();
  }

  function handleRomUpload(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (ev) => {
      const data = new Uint8Array(ev.target!.result as ArrayBuffer);
      mattbrewBoard.loadRom(data);
      mattbrewBoard.reset(true);
      mattbrewBoard.boot();
      refresh();
    };
    reader.readAsArrayBuffer(file);
    e.target.value = "";
  }

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
      </div>
      <LcdWidget via={mattbrewBoard.bus.via} />
      <div className="main-layout">
        <CpuWidget cpu={mattbrewBoard.getCpu()} />
        <div className="memory-panels">
          <PageBrowser memory={mattbrewBoard.getBus()} sp={mattbrewBoard.getCpu().state.s} />
          <StackDisplay memory={mattbrewBoard.getBus()} sp={mattbrewBoard.getCpu().state.s} />
        </div>
      </div>
    </div>
  );
}

function CpuWidget({ cpu }: { cpu: CpuInterface }) {
  const s = cpu.state;
  const disasmLines = dbg.disassemble(20).split("\n");
  const disasmMarked = disasmLines
    .map((line, i) => (i === 0 ? "> " + line : "  " + line))
    .join("\n");

  return (
    <div className="cpu-widget">
      <h2>CPU</h2>
      <table className="registers">
        <tbody>
          <tr><th>PC</th><td>${s.p.toString(16).padStart(4, "0").toUpperCase()}</td></tr>
          <tr><th>SP</th><td>${s.s.toString(16).padStart(2, "0").toUpperCase()}</td></tr>
          <tr><th>A</th><td>${s.a.toString(16).padStart(2, "0").toUpperCase()}</td></tr>
          <tr><th>X</th><td>${s.x.toString(16).padStart(2, "0").toUpperCase()}</td></tr>
          <tr><th>Y</th><td>${s.y.toString(16).padStart(2, "0").toUpperCase()}</td></tr>
        </tbody>
      </table>
      <h3>Disassembly</h3>
      <pre className="disassembly">{disasmMarked}</pre>
    </div>
  );
}

function PageDisplay({ memory, baseAddress, marker }: { memory: BusInterface; baseAddress: number; marker?: number }) {
  const bytesPerRow = 16;
  const rows = [];
  for (let i = 0; i < 16; i++) {
    const rowAddress = baseAddress + i * bytesPerRow;
    const markerRow = marker !== undefined && Math.floor(marker / bytesPerRow) === i;
    const markerCol = marker !== undefined ? marker % bytesPerRow : -1;
    const cells = [];
    for (let j = 0; j < bytesPerRow; j++) {
      let byte: string;
      try {
        byte = memory.peek(rowAddress + j).toString(16).padStart(2, "0").toUpperCase();
      } catch {
        byte = "??";
      }
      cells.push(
        <td key={j} className={markerRow && j === markerCol ? "marker-cell" : undefined}>
          {byte}
        </td>
      );
    }
    rows.push(
      <tr key={i} className={markerRow ? "marker-row" : undefined}>
        <td className="addr">{rowAddress.toString(16).padStart(4, "0").toUpperCase()}</td>
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

function PageBrowser({ memory, sp }: { memory: BusInterface; sp: number }) {
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
        memory={memory}
        baseAddress={page * 0x100}
        marker={page === 0x01 ? sp : undefined}
      />
    </div>
  );
}

function StackDisplay({ memory, sp }: { memory: BusInterface; sp: number }) {
  return (
    <div>
      <h3>Stack (SP=${sp.toString(16).padStart(2, "0").toUpperCase()})</h3>
      <PageDisplay memory={memory} baseAddress={0x0100} marker={sp} />
    </div>
  );
}

function LcdWidget({ via }: { via: ViaLcd }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const lcd = via.getLcd();
    if (!lcd || !canvasRef.current) return;
    const ctx = canvasRef.current.getContext("2d");
    if (ctx) lcd.render(ctx, 0, 0, canvasRef.current.width, canvasRef.current.height);
  });

  return (
    <div className="lcd-widget">
      <h3>LCD</h3>
      <canvas ref={canvasRef} width={800} height={160} className="lcd-canvas" />
    </div>
  );
}

export default App;
