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
  /** JSON results of the memory, JIT and FEXCore self-tests. */
  runSelfTest(): Promise<string>;
  /** Connected game controllers (GameController framework). */
  controllers(): Array<{ name: string; category: string; kind: string; haptics: boolean; light: boolean; battery: number }>;
  /** Progress lines of every self-test run so far (survives a crash). */
  readLog(): string;
  clearLog(): void;
}

export default requireNativeModule<Gow3CoreModule>('Gow3Core');
