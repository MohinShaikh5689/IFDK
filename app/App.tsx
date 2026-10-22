import { StatusBar } from 'expo-status-bar';
import { useCallback, useEffect, useState } from 'react';
import { AppState, Pressable, ScrollView, Share, StyleSheet, Text, View } from 'react-native';

import Gow3Core from './modules/gow3-core/src/Gow3CoreModule';

const POOL_MB = 256;

type Pairing = Awaited<ReturnType<typeof Gow3Core.pairingState>>;
type BuiltIn = Awaited<ReturnType<typeof Gow3Core.builtInJitStatus>>;

// controllers() is new in the native build after 4ed13b6; older installed IPAs lack it.
const listControllers = () => (typeof Gow3Core.controllers === 'function' ? Gow3Core.controllers() : []);

function pretty(json: string): string {
  try {
    return JSON.stringify(JSON.parse(json), null, 2);
  } catch {
    return json;
  }
}

function Button({ title, onPress, disabled }: { title: string; onPress: () => void; disabled?: boolean }) {
  return (
    <Pressable style={[styles.button, disabled && styles.buttonDisabled]} onPress={onPress} disabled={disabled}>
      <Text style={styles.buttonText}>{title}</Text>
    </Pressable>
  );
}

export default function App() {
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('JIT + FEXCore self-test');
  const [report, setReport] = useState(() => pretty(Gow3Core.deviceReport()));
  const [result, setResult] = useState('');
  const [log, setLog] = useState(() => Gow3Core.readLog());
  const [pads, setPads] = useState(listControllers);
  const [builtIn, setBuiltIn] = useState<BuiltIn | null>(null);
  const [pairing, setPairing] = useState<Pairing | null>(null);
  const [files, setFiles] = useState(() => Gow3Core.gameFiles());
  const [gameLog, setGameLog] = useState(() => Gow3Core.readGameLog());
  const [gpuReport, setGpuReport] = useState('');

  const refresh = useCallback(() => {
    setReport(pretty(Gow3Core.deviceReport()));
    setLog(Gow3Core.readLog());
    setPads(listControllers());
    Gow3Core.builtInJitStatus().then(setBuiltIn, () => setBuiltIn(null));
    Gow3Core.pairingState().then(setPairing, () => setPairing(null));
    setFiles(Gow3Core.gameFiles());
    setGameLog(Gow3Core.readGameLog());
  }, []);

  useEffect(() => {
    refresh();
    // Pairing happens in Settings: refresh when the app comes back.
    const sub = AppState.addEventListener('change', (state) => state === 'active' && refresh());
    return () => sub.remove();
  }, [refresh]);

  // While the game runs, follow its log.
  useEffect(() => {
    if (!files.started) return;
    const timer = setInterval(() => setGameLog(Gow3Core.readGameLog()), 1000);
    return () => clearInterval(timer);
  }, [files.started]);

  const pairingActive = pairing?.phase === 'waiting' || pairing?.phase === 'pin';
  useEffect(() => {
    if (!pairingActive) return;
    const timer = setInterval(() => {
      Gow3Core.pairingState().then((p) => {
        setPairing(p);
        if (p.phase === 'paired') refresh();
      });
    }, 1000);
    return () => clearInterval(timer);
  }, [pairingActive, refresh]);

  const run = useCallback(
    async (what: () => Promise<string>) => {
      setBusy(true);
      try {
        setStatus(await what());
      } catch (e) {
        setStatus(String(e));
      } finally {
        refresh();
        setBusy(false);
      }
    },
    [refresh]
  );

  const enableJit = useCallback(
    () =>
      run(async () => {
        const ready = (code: number) => (code === 0 ? `JIT pool ready (${POOL_MB} MiB)` : `JIT pool failed: ${code}`);
        const status = await Gow3Core.builtInJitStatus();
        if (Gow3Core.isDebuggerAttached() || (status.available && status.paired)) {
          setStatus('Attaching the built-in debugger…');
          return ready(await Gow3Core.enableJitBuiltIn(POOL_MB));
        }
        if (await Gow3Core.isStikDebugInstalled()) {
          setStatus('Waiting for StikDebug to attach…');
          return ready(await Gow3Core.enableJit(POOL_MB));
        }
        return status.available
          ? 'Pair this iPhone first (Pair in app, below), and connect LocalDevVPN.'
          : `Built-in JIT unavailable: ${status.reason}`;
      }),
    [run]
  );

  const runSelfTest = useCallback(async () => {
    setBusy(true);
    setStatus('Running self-test…');
    try {
      setResult(pretty(await Gow3Core.runSelfTest()));
      setStatus('Self-test finished');
    } catch (e) {
      setStatus(`Self-test: ${String(e)}`);
    } finally {
      refresh();
      setBusy(false);
    }
  }, [refresh]);

  return (
    <View style={styles.root}>
      <ScrollView contentContainerStyle={styles.content}>
        <Text style={styles.title}>gow3-ios</Text>
        <Text style={styles.status}>{status}</Text>
        <View style={styles.row}>
          <Button title="Enable JIT" onPress={enableJit} disabled={busy} />
          <Button title="Run self-test" onPress={runSelfTest} disabled={busy} />
        </View>
        <View style={styles.row}>
          <Button title="Refresh" onPress={refresh} disabled={busy} />
          <Button
            title="Clear log"
            onPress={() => {
              Gow3Core.clearLog();
              refresh();
            }}
            disabled={busy}
          />
        </View>
        <Text style={styles.heading}>JIT (built in)</Text>
        <Text style={styles.mono}>
          {builtIn
            ? builtIn.available
              ? builtIn.paired
                ? 'Paired. Connect LocalDevVPN, then Check setup (first time) and Enable JIT.'
                : builtIn.pairingSupported
                  ? 'Not paired. Tap Pair in app, then go to Settings > Privacy & Security > Developer Mode and tap Pair with gow3-ios.'
                  : 'Not paired. In-app pairing needs iOS 27.'
              : builtIn.reason
            : '…'}
        </Text>
        {pairing && pairing.phase !== 'idle' ? (
          <View style={styles.pairing}>
            {pairing.phase === 'pin' ? <Text style={styles.pin} selectable>{pairing.pin}</Text> : null}
            <Text style={styles.mono}>{pairing.message}</Text>
            {pairingActive && pairing.backgroundLimited ? (
              <Text style={styles.warn}>iOS only lets this install wait about 30 s in the background: go to Settings straight away.</Text>
            ) : null}
          </View>
        ) : null}
        <View style={styles.row}>
          {pairingActive ? (
            <Button title="Cancel pairing" onPress={() => Gow3Core.cancelPairing().then(refresh)} />
          ) : (
            <Button
              title={builtIn?.paired ? 'Pair again' : 'Pair in app'}
              onPress={() => Gow3Core.startPairing().then(refresh)}
              disabled={busy || !builtIn?.available || !builtIn?.pairingSupported}
            />
          )}
          <Button
            title="Check setup"
            onPress={() => run(async () => (setStatus('Checking LocalDevVPN and the Developer Disk Image…'), await Gow3Core.prepareBuiltInJit()))}
            disabled={busy || !builtIn?.paired || pairingActive}
          />
        </View>
        <View style={styles.row}>
          <Button title="Reset disk image" onPress={() => run(() => Gow3Core.resetDeveloperDiskImage())} disabled={busy || !builtIn?.available} />
          <Button title="Forget pairing" onPress={() => Gow3Core.forgetPairing().then(refresh)} disabled={busy || !builtIn?.paired || pairingActive} />
        </View>
        <Text style={styles.heading}>Game</Text>
        <Text style={styles.mono}>
          {`Files app > On My iPhone > gow3-ios > gow3:\n` +
            `  boot-linked.bin ${files.boot ? 'ok' : 'MISSING'}, content.bin ${files.content ? 'ok' : 'missing'}, patches.bin ${files.patches ? 'ok' : 'missing'}\n` +
            `  game/eboot.bin ${files.eboot ? 'ok' : 'MISSING'}, game/sce_sys/param.sfo ${files.sfo ? 'ok' : 'missing'}`}
        </Text>
        <View style={styles.row}>
          <Button
            title={files.started ? 'Game running' : 'Start game'}
            onPress={() => run(() => Gow3Core.startGame())}
            disabled={busy || files.started || !files.boot || !files.eboot}
          />
          {files.started ? null : (
            <Button
              title="CPU only"
              onPress={() => run(() => Gow3Core.startGameCpuOnly())}
              disabled={busy || !files.boot || !files.eboot}
            />
          )}
          {files.started ? (
            <Button
              title="Dump threads"
              onPress={() => run(async () => `Thread dump: ${await Gow3Core.dumpGameThreads()} threads (see the game log)`)}
              disabled={busy}
            />
          ) : null}
          <Button
            title="Copy logs"
            onPress={() => {
              // The share sheet has Copy (and AirDrop, Notes, ...): no clipboard module is built in.
              const text = Gow3Core.readGameLog();
              if (text) Share.share({ message: text }).catch(() => {});
            }}
            disabled={!gameLog}
          />
          <Button
            title="Clear game log"
            onPress={() => {
              Gow3Core.clearGameLog();
              refresh();
            }}
            disabled={busy}
          />
        </View>
        {gameLog ? (
          <Text style={styles.mono} selectable>
            {gameLog}
          </Text>
        ) : null}
        <Text style={styles.heading}>GPU (MoltenVK)</Text>
        <Text style={styles.mono}>Clears the screen to a cycling colour for 5 s, then lists what shadPS4 would lack.</Text>
        <View style={styles.row}>
          <Button
            title="GPU screen test"
            onPress={() =>
              run(async () => {
                const json = await Gow3Core.gpuScreenTest(5);
                setGpuReport(pretty(json));
                return JSON.parse(json).ok ? 'GPU screen test passed' : 'GPU screen test failed (see the report)';
              })
            }
            disabled={busy}
          />
        </View>
        {gpuReport ? (
          <Text style={styles.mono} selectable>
            {gpuReport}
          </Text>
        ) : null}
        <Text style={styles.heading}>Controllers</Text>
        <Text style={styles.mono} selectable>
          {pads.length ? pads.map((p) => `${p.kind}: ${p.name} (${p.category})${p.haptics ? ', haptics' : ''}${p.light ? ', light' : ''}`).join('\n') : 'none connected (pair in Settings → Bluetooth, then Refresh)'}
        </Text>
        <Text style={styles.heading}>Device</Text>
        <Text style={styles.mono} selectable>{report}</Text>
        {result ? (
          <>
            <Text style={styles.heading}>Self-test</Text>
            <Text style={styles.mono} selectable>{result}</Text>
          </>
        ) : null}
        <Text style={styles.heading}>Log (survives crashes)</Text>
        <Text style={styles.mono} selectable>{log || '(empty)'}</Text>
      </ScrollView>
      <StatusBar style="light" />
    </View>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: '#111' },
  content: { padding: 16, paddingTop: 64, gap: 12 },
  title: { color: '#fff', fontSize: 28, fontWeight: '700' },
  status: { color: '#f5c542', fontSize: 15 },
  row: { flexDirection: 'row', gap: 12 },
  button: { flex: 1, backgroundColor: '#b3261e', paddingVertical: 12, borderRadius: 10, alignItems: 'center' },
  buttonDisabled: { opacity: 0.4 },
  buttonText: { color: '#fff', fontWeight: '600' },
  heading: { color: '#aaa', fontSize: 13, fontWeight: '600', marginTop: 8, textTransform: 'uppercase' },
  mono: { color: '#ddd', fontFamily: 'Menlo', fontSize: 11 },
  pairing: { gap: 6, padding: 12, borderRadius: 10, backgroundColor: '#1d1d1d' },
  pin: { color: '#fff', fontFamily: 'Menlo', fontSize: 40, fontWeight: '700', letterSpacing: 6, textAlign: 'center' },
  warn: { color: '#f5a142', fontSize: 12 },
});
