import { NativeModule, requireNativeModule } from 'expo';

declare class Gow3CoreModule extends NativeModule<{}> {
  /** JSON: model, RAM, task address map, footprint and limit, signing flags, JIT pool state. */
  deviceReport(): string;
  /** A debugger (StikDebug) is attached right now. */
  isDebuggerAttached(): boolean;
  /** CS_DEBUGGED is set (stays set after the debugger detaches). */
  isCsDebugged(): boolean;
  isStikDebugInstalled(): Promise<boolean>;
  /** Opens StikDebug, waits for it, creates the JIT pool and detaches. Resolves 0 on success. */
  enableJit(poolMb: number): Promise<number>;
  /** Built-in JIT: whether this install can use it (iOS 26+, helper extension present) and is paired. */
  builtInJitStatus(): Promise<{ available: boolean; reason: string; paired: boolean; pairingSupported: boolean }>;
  /** In-app pairing (iOS 27+): advertises the app as a computer to pair with from Settings. */
  startPairing(): Promise<void>;
  cancelPairing(): Promise<void>;
  /** Deletes the stored pairing file. */
  forgetPairing(): Promise<void>;
  pairingState(): Promise<{ phase: 'idle' | 'waiting' | 'pin' | 'paired' | 'failed'; pin: string; message: string; backgroundLimited: boolean; supported: boolean; paired: boolean }>;
  /** Checks LocalDevVPN and the Developer Disk Image (downloads and mounts it if needed). */
  prepareBuiltInJit(): Promise<string>;
  resetDeveloperDiskImage(): Promise<string>;
  /** The app's helper attaches the debugger, then the JIT pool is created and it detaches. Resolves 0 on success. */
  enableJitBuiltIn(poolMb: number): Promise<number>;
  /** Documents/gow3: which of the loader's files are there (from the PC's gow3 out/ folder, and game/). */
  gameFiles(): { folder: string; boot: boolean; content: boolean; patches: boolean; eboot: boolean; sfo: boolean; started: boolean };
  /** Runs the game CPU-only (no GPU yet) through FEXCore; output goes to Documents/gow3/loader.log. */
  startGameCpuOnly(): Promise<string>;
  /** The last 24 KiB of the loader log. */
  readGameLog(): string;
  clearGameLog(): void;
  /** JSON results of the memory, JIT and FEXCore self-tests. */
  runSelfTest(): Promise<string>;
  /** Connected game controllers (GameController framework). */
  controllers(): Array<{ name: string; category: string; kind: string; haptics: boolean; light: boolean; battery: number }>;
  /** Progress lines of every self-test run so far (survives a crash). */
  readLog(): string;
  clearLog(): void;
}

export default requireNativeModule<Gow3CoreModule>('Gow3Core');
