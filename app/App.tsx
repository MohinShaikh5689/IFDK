import { StatusBar } from 'expo-status-bar';
import { type ReactNode, useCallback, useEffect, useState } from 'react';
import { AppState, Image, Pressable, ScrollView, Share, StyleSheet, Switch, Text, View } from 'react-native';

import Gow3Core from './modules/gow3-core/src/Gow3CoreModule';

const APP_NAME = 'Olympus';
const POOL_MB = 256;

type Pairing = Awaited<ReturnType<typeof Gow3Core.pairingState>>;
type BuiltIn = Awaited<ReturnType<typeof Gow3Core.builtInJitStatus>>;

// controllers() is new in the native build after 4ed13b6; older installed IPAs lack it.
const listControllers = () => (typeof Gow3Core.controllers === 'function' ? Gow3Core.controllers() : []);
// The on-screen controls switch is new in 0.1.44, the FPS cap in 0.1.45; older installed IPAs lack them.
const hasTouchControls = typeof Gow3Core.touchControlsEnabled === 'function';
const hasFpsLimit = typeof Gow3Core.fpsLimit === 'function';
const FPS_LIMITS: Array<[number, string]> = [
  [60, '60'],
  [30, '30'],
  [0, 'Off'],
];

const color = {
  bg: '#0b0b0e',
  card: '#15151a',
  raised: '#1d1d24',
  border: '#272730',
  text: '#f3f1ee',
  muted: '#9b98a3',
  faint: '#5f5c66',
  ember: '#d0402f',
  emberDeep: '#8f2219',
  gold: '#e3b04b',
  ok: '#4cc38a',
  warn: '#f0a04b',
};

function pretty(json: string): string {
  try {
    return JSON.stringify(JSON.parse(json), null, 2);
  } catch {
    return json;
  }
}

function jitReady(report: string): boolean {
  try {
    return JSON.parse(report).jit_pool === true;
  } catch {
    return false;
  }
}

function Card({ children, style }: { children: ReactNode; style?: object }) {
  return <View style={[styles.card, style]}>{children}</View>;
}

function SectionTitle({ children }: { children: string }) {
  return <Text style={styles.sectionTitle}>{children}</Text>;
}

function Button({
  title,
  onPress,
  disabled,
  kind = 'secondary',
}: {
  title: string;
  onPress: () => void;
  disabled?: boolean;
  kind?: 'primary' | 'secondary' | 'quiet';
}) {
  return (
    <Pressable
      onPress={onPress}
      disabled={disabled}
      style={({ pressed }) => [
        styles.button,
        kind === 'primary' && styles.buttonPrimary,
        kind === 'quiet' && styles.buttonQuiet,
        pressed && styles.buttonPressed,
        disabled && styles.buttonDisabled,
      ]}
    >
      <Text style={[styles.buttonText, kind === 'primary' && styles.buttonTextPrimary]}>{title}</Text>
    </Pressable>
  );
}

function Tile({ label, value, good }: { label: string; value: string; good: boolean }) {
  return (
    <View style={styles.tile}>
      <View style={styles.tileHead}>
        <View style={[styles.dot, { backgroundColor: good ? color.ok : color.warn }]} />
        <Text style={styles.tileLabel}>{label}</Text>
      </View>
      <Text style={styles.tileValue} numberOfLines={1}>
        {value}
      </Text>
    </View>
  );
}

function Segmented({ options, value, onChange }: { options: Array<[number, string]>; value: number; onChange: (v: number) => void }) {
  return (
    <View style={styles.segmented}>
      {options.map(([v, label]) => (
        <Pressable key={label} onPress={() => onChange(v)} style={[styles.segment, value === v && styles.segmentOn]}>
          <Text style={[styles.segmentText, value === v && styles.segmentTextOn]}>{label}</Text>
        </Pressable>
      ))}
    </View>
  );
}

function SettingRow({ title, detail, children }: { title: string; detail?: string; children: ReactNode }) {
  return (
    <View style={styles.settingRow}>
      <View style={styles.settingText}>
        <Text style={styles.settingTitle}>{title}</Text>
        {detail ? <Text style={styles.settingDetail}>{detail}</Text> : null}
      </View>
      {children}
    </View>
  );
}

function Collapsible({ title, subtitle, open, onToggle, children }: { title: string; subtitle?: string; open: boolean; onToggle: () => void; children: ReactNode }) {
  return (
    <Card>
      <Pressable onPress={onToggle} style={styles.collapsibleHead}>
        <View style={styles.settingText}>
          <Text style={styles.cardTitle}>{title}</Text>
          {subtitle ? <Text style={styles.settingDetail}>{subtitle}</Text> : null}
        </View>
        <Text style={styles.chevron}>{open ? '▾' : '▸'}</Text>
      </Pressable>
      {open ? <View style={styles.collapsibleBody}>{children}</View> : null}
    </Card>
  );
}

function FileLine({ name, present, required }: { name: string; present: boolean; required?: boolean }) {
  return (
    <View style={styles.fileLine}>
      <Text style={[styles.fileMark, { color: present ? color.ok : required ? color.ember : color.faint }]}>{present ? '✓' : '✕'}</Text>
      <Text style={styles.fileName}>{name}</Text>
      {!present ? <Text style={styles.fileHint}>{required ? 'required' : 'optional'}</Text> : null}
    </View>
  );
}

export default function App() {
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('');
  const [report, setReport] = useState(() => Gow3Core.deviceReport());
  const [result, setResult] = useState('');
  const [log, setLog] = useState(() => Gow3Core.readLog());
  const [pads, setPads] = useState(listControllers);
  const [builtIn, setBuiltIn] = useState<BuiltIn | null>(null);
  const [pairing, setPairing] = useState<Pairing | null>(null);
  const [files, setFiles] = useState(() => Gow3Core.gameFiles());
  const [gameLog, setGameLog] = useState(() => Gow3Core.readGameLog());
  const [gpuReport, setGpuReport] = useState('');
  const [fpsLimit, setFpsLimit] = useState(() => (hasFpsLimit ? Gow3Core.fpsLimit() : 60));
  const [touchControls, setTouchControls] = useState(() => (hasTouchControls ? Gow3Core.touchControlsEnabled() : false));
  const [jitOpen, setJitOpen] = useState(false);
  const [filesOpen, setFilesOpen] = useState(false);
  const [devOpen, setDevOpen] = useState(false);

  const refresh = useCallback(() => {
    setReport(Gow3Core.deviceReport());
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
        const ready = (code: number) => (code === 0 ? 'JIT is ready.' : `JIT setup failed (${code}).`);
        const status = await Gow3Core.builtInJitStatus();
        if (Gow3Core.isDebuggerAttached() || (status.available && status.paired)) {
          setStatus('Enabling JIT…');
          return ready(await Gow3Core.enableJitBuiltIn(POOL_MB));
        }
        if (await Gow3Core.isStikDebugInstalled()) {
          setStatus('Waiting for StikDebug…');
          return ready(await Gow3Core.enableJit(POOL_MB));
        }
        setJitOpen(true);
        return status.available ? 'Pair this iPhone first (JIT setup, below), and connect LocalDevVPN.' : `JIT unavailable: ${status.reason}`;
      }),
    [run]
  );

  const runSelfTest = useCallback(async () => {
    setBusy(true);
    setStatus('Running self-test…');
    try {
      setResult(pretty(await Gow3Core.runSelfTest()));
      setStatus('Self-test finished.');
    } catch (e) {
      setStatus(`Self-test: ${String(e)}`);
    } finally {
      refresh();
      setBusy(false);
    }
  }, [refresh]);

  const jit = jitReady(report);
  const filesReady = files.boot && files.eboot;
  const missing = [!files.boot && 'boot-linked.bin', !files.eboot && 'eboot.bin'].filter(Boolean).length;
  const pad = pads[0];

  let play: { title: string; onPress: () => void; disabled: boolean; hint: string };
  if (files.started) {
    play = { title: 'Running', onPress: () => {}, disabled: true, hint: 'The game is running. Two-finger double tap in game returns here.' };
  } else if (!filesReady) {
    play = { title: 'Add game files', onPress: () => setFilesOpen(true), disabled: false, hint: 'Copy your own game dump in with the Files app.' };
  } else if (!jit) {
    play = { title: 'Enable JIT', onPress: enableJit, disabled: busy, hint: 'JIT is needed once per launch of the app.' };
  } else {
    play = { title: 'Play', onPress: () => run(() => Gow3Core.startGame()), disabled: busy, hint: 'Ready.' };
  }

  const jitText = builtIn
    ? builtIn.available
      ? builtIn.paired
        ? 'Paired. Connect LocalDevVPN, run Check setup once, then Enable JIT.'
        : builtIn.pairingSupported
          ? `Not paired. Tap Pair, then open Settings › Privacy & Security › Developer Mode and tap “Pair with gow3-ios”.`
          : 'Not paired. In-app pairing needs iOS 27; StikDebug works too.'
      : builtIn.reason
    : 'Checking…';

  return (
    <View style={styles.root}>
      <ScrollView contentContainerStyle={styles.content}>
        <View style={styles.header}>
          <Image source={require('./assets/icon.png')} style={styles.logo} />
          <View>
            <Text style={styles.title}>{APP_NAME}</Text>
            <Text style={styles.subtitle}>God of War III Remastered · PS4 port</Text>
          </View>
        </View>

        <Card style={styles.hero}>
          <Text style={styles.heroLabel}>{files.started ? 'IN GAME' : 'READY TO ASCEND'}</Text>
          <Button title={play.title} onPress={play.onPress} disabled={play.disabled} kind="primary" />
          <Text style={styles.heroHint}>{status || play.hint}</Text>
        </Card>

        <View style={styles.tiles}>
          <Tile label="JIT" value={jit ? 'Ready' : builtIn?.paired ? 'Paired' : 'Needed'} good={jit} />
          <Tile label="Game" value={filesReady ? 'Ready' : `${missing} missing`} good={filesReady} />
          <Tile label="Input" value={pad ? pad.name : touchControls ? 'Touch' : 'None'} good={!!pad || touchControls} />
        </View>

        <SectionTitle>Settings</SectionTitle>
        <Card>
          {hasFpsLimit ? (
            <SettingRow title="Frame rate cap" detail={files.started ? 'Applies at the next start' : '30 is cooler and steadier'}>
              <Segmented
                options={FPS_LIMITS}
                value={fpsLimit}
                onChange={(v) => {
                  Gow3Core.setFpsLimit(v);
                  setFpsLimit(v);
                }}
              />
            </SettingRow>
          ) : null}
          {hasTouchControls ? (
            <SettingRow title="On-screen controls" detail="Hidden while a controller is connected. Long-press Hide in game to edit the layout.">
              <Switch
                value={touchControls}
                onValueChange={(v) => {
                  Gow3Core.setTouchControlsEnabled(v);
                  setTouchControls(v);
                }}
                trackColor={{ true: color.ember, false: color.border }}
              />
            </SettingRow>
          ) : null}
          <SettingRow title="Controller" detail={pad ? `${pad.kind}${pad.haptics ? ' · haptics' : ''}${pad.battery >= 0 ? ` · ${Math.round(pad.battery * 100)}%` : ''}` : 'Pair one in Settings › Bluetooth'}>
            <Text style={styles.settingValue}>{pad ? pad.name : 'None'}</Text>
          </SettingRow>
        </Card>

        <SectionTitle>Setup</SectionTitle>
        <Collapsible title="JIT" subtitle={jit ? 'Ready for this session' : builtIn?.paired ? 'Paired' : 'Pairing needed'} open={jitOpen} onToggle={() => setJitOpen(!jitOpen)}>
          <Text style={styles.body}>{jitText}</Text>
          {pairing && pairing.phase !== 'idle' ? (
            <View style={styles.pairing}>
              {pairing.phase === 'pin' ? (
                <Text style={styles.pin} selectable>
                  {pairing.pin}
                </Text>
              ) : null}
              <Text style={styles.body}>{pairing.message}</Text>
              {pairingActive && pairing.backgroundLimited ? <Text style={styles.warn}>iOS only waits about 30 s in the background: go to Settings straight away.</Text> : null}
            </View>
          ) : null}
          <View style={styles.row}>
            <Button title="Enable JIT" onPress={enableJit} disabled={busy || jit} />
            {pairingActive ? (
              <Button title="Cancel pairing" onPress={() => Gow3Core.cancelPairing().then(refresh)} />
            ) : (
              <Button
                title={builtIn?.paired ? 'Pair again' : 'Pair'}
                onPress={() => Gow3Core.startPairing().then(refresh)}
                disabled={busy || !builtIn?.available || !builtIn?.pairingSupported}
              />
            )}
          </View>
          <View style={styles.row}>
            <Button
              title="Check setup"
              onPress={() => run(async () => (setStatus('Checking LocalDevVPN and the Developer Disk Image…'), await Gow3Core.prepareBuiltInJit()))}
              disabled={busy || !builtIn?.paired || pairingActive}
            />
            <Button title="Reset disk image" onPress={() => run(() => Gow3Core.resetDeveloperDiskImage())} disabled={busy || !builtIn?.available} kind="quiet" />
          </View>
          <Button title="Forget pairing" onPress={() => Gow3Core.forgetPairing().then(refresh)} disabled={busy || !builtIn?.paired || pairingActive} kind="quiet" />
        </Collapsible>

        <Collapsible title="Game files" subtitle={filesReady ? 'All required files found' : `${missing} required file${missing === 1 ? '' : 's'} missing`} open={filesOpen} onToggle={() => setFilesOpen(!filesOpen)}>
          <Text style={styles.body}>
            {`Files app › On My iPhone › ${APP_NAME} › gow3. Use your own legally dumped copy (CUSA01623/01715/01720, update 01.02) and the files the PC prepare step makes.`}
          </Text>
          <View style={styles.fileList}>
            <FileLine name="boot-linked.bin" present={files.boot} required />
            <FileLine name="game/eboot.bin" present={files.eboot} required />
            <FileLine name="game/sce_sys/param.sfo" present={files.sfo} />
            <FileLine name="content.bin" present={files.content} />
            <FileLine name="patches.bin" present={files.patches} />
          </View>
          <Button title="Check again" onPress={refresh} />
        </Collapsible>

        <SectionTitle>Advanced</SectionTitle>
        <Collapsible title="Developer" subtitle="Logs, tests and diagnostics" open={devOpen} onToggle={() => setDevOpen(!devOpen)}>
          <View style={styles.row}>
            <Button title="Self-test" onPress={runSelfTest} disabled={busy} />
            <Button
              title="GPU test"
              onPress={() =>
                run(async () => {
                  const json = await Gow3Core.gpuScreenTest(5);
                  setGpuReport(pretty(json));
                  return JSON.parse(json).ok ? 'GPU screen test passed.' : 'GPU screen test failed (see the report).';
                })
              }
              disabled={busy || files.started}
            />
          </View>
          <View style={styles.row}>
            {files.started ? (
              <Button title="Dump threads" onPress={() => run(async () => `Thread dump: ${await Gow3Core.dumpGameThreads()} threads (see the game log).`)} disabled={busy} />
            ) : (
              <Button title="CPU only" onPress={() => run(() => Gow3Core.startGameCpuOnly())} disabled={busy || !filesReady} />
            )}
            <Button
              title="Share game log"
              onPress={() => {
                const text = Gow3Core.readGameLog();
                if (text) Share.share({ message: text }).catch(() => {});
              }}
              disabled={!gameLog}
            />
          </View>
          <View style={styles.row}>
            <Button
              title="Clear game log"
              onPress={() => {
                Gow3Core.clearGameLog();
                refresh();
              }}
              disabled={busy}
              kind="quiet"
            />
            <Button
              title="Clear test log"
              onPress={() => {
                Gow3Core.clearLog();
                refresh();
              }}
              disabled={busy}
              kind="quiet"
            />
          </View>
          {gameLog ? <Mono title="Game log" text={gameLog} /> : null}
          {gpuReport ? <Mono title="GPU test" text={gpuReport} /> : null}
          {result ? <Mono title="Self-test" text={result} /> : null}
          <Mono title="Controllers" text={pads.length ? pads.map((p) => `${p.kind}: ${p.name} (${p.category})`).join('\n') : 'none'} />
          <Mono title="Device" text={pretty(report)} />
          <Mono title="Test log (survives crashes)" text={log || '(empty)'} />
        </Collapsible>

        <Text style={styles.footer}>A fan port that runs your own copy. Not affiliated with Sony Interactive Entertainment or Santa Monica Studio.</Text>
      </ScrollView>
      <StatusBar style="light" />
    </View>
  );
}

function Mono({ title, text }: { title: string; text: string }) {
  return (
    <View style={styles.mono}>
      <Text style={styles.monoTitle}>{title}</Text>
      <Text style={styles.monoText} selectable>
        {text}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: color.bg },
  content: { padding: 18, paddingTop: 68, paddingBottom: 48, gap: 14 },
  header: { flexDirection: 'row', alignItems: 'center', gap: 14, marginBottom: 4 },
  logo: { width: 56, height: 56, borderRadius: 13 },
  title: { color: color.text, fontSize: 30, fontWeight: '800', letterSpacing: 0.5 },
  subtitle: { color: color.muted, fontSize: 13, marginTop: 2 },
  card: { backgroundColor: color.card, borderRadius: 18, borderWidth: StyleSheet.hairlineWidth, borderColor: color.border, padding: 16, gap: 12 },
  hero: { backgroundColor: '#1a1012', borderColor: color.emberDeep, paddingVertical: 22, gap: 14 },
  heroLabel: { color: color.gold, fontSize: 12, fontWeight: '700', letterSpacing: 3 },
  heroHint: { color: color.muted, fontSize: 13, textAlign: 'center' },
  tiles: { flexDirection: 'row', gap: 10 },
  tile: { flex: 1, backgroundColor: color.card, borderRadius: 14, borderWidth: StyleSheet.hairlineWidth, borderColor: color.border, padding: 12, gap: 6 },
  tileHead: { flexDirection: 'row', alignItems: 'center', gap: 6 },
  tileLabel: { color: color.muted, fontSize: 11, fontWeight: '600', letterSpacing: 1, textTransform: 'uppercase' },
  tileValue: { color: color.text, fontSize: 15, fontWeight: '600' },
  dot: { width: 7, height: 7, borderRadius: 4 },
  sectionTitle: { color: color.faint, fontSize: 12, fontWeight: '700', letterSpacing: 1.5, textTransform: 'uppercase', marginTop: 10, marginLeft: 4 },
  cardTitle: { color: color.text, fontSize: 17, fontWeight: '700' },
  row: { flexDirection: 'row', gap: 10 },
  button: { flex: 1, backgroundColor: color.raised, borderRadius: 12, paddingVertical: 13, alignItems: 'center', borderWidth: StyleSheet.hairlineWidth, borderColor: color.border },
  buttonPrimary: { backgroundColor: color.ember, borderColor: color.ember, paddingVertical: 18, borderRadius: 14, shadowColor: color.ember, shadowOpacity: 0.45, shadowRadius: 16, shadowOffset: { width: 0, height: 6 } },
  buttonQuiet: { backgroundColor: 'transparent' },
  buttonPressed: { opacity: 0.75 },
  buttonDisabled: { opacity: 0.4 },
  buttonText: { color: color.text, fontWeight: '600', fontSize: 15 },
  buttonTextPrimary: { fontSize: 20, fontWeight: '800', letterSpacing: 1 },
  settingRow: { flexDirection: 'row', alignItems: 'center', gap: 12, minHeight: 44 },
  settingText: { flex: 1, gap: 3 },
  settingTitle: { color: color.text, fontSize: 16, fontWeight: '600' },
  settingDetail: { color: color.muted, fontSize: 12 },
  settingValue: { color: color.muted, fontSize: 14, maxWidth: 140 },
  segmented: { flexDirection: 'row', backgroundColor: color.raised, borderRadius: 10, padding: 3 },
  segment: { paddingHorizontal: 14, paddingVertical: 7, borderRadius: 8 },
  segmentOn: { backgroundColor: color.ember },
  segmentText: { color: color.muted, fontWeight: '600' },
  segmentTextOn: { color: '#fff' },
  collapsibleHead: { flexDirection: 'row', alignItems: 'center' },
  collapsibleBody: { gap: 12 },
  chevron: { color: color.muted, fontSize: 18, paddingLeft: 8 },
  body: { color: color.muted, fontSize: 14, lineHeight: 20 },
  pairing: { gap: 8, padding: 14, borderRadius: 12, backgroundColor: color.raised },
  pin: { color: color.text, fontFamily: 'Menlo', fontSize: 40, fontWeight: '700', letterSpacing: 6, textAlign: 'center' },
  warn: { color: color.warn, fontSize: 12 },
  fileList: { gap: 8, paddingVertical: 4 },
  fileLine: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  fileMark: { fontSize: 15, fontWeight: '800', width: 16, textAlign: 'center' },
  fileName: { color: color.text, fontFamily: 'Menlo', fontSize: 13, flex: 1 },
  fileHint: { color: color.faint, fontSize: 12 },
  mono: { backgroundColor: '#0f0f13', borderRadius: 10, padding: 10, gap: 6 },
  monoTitle: { color: color.gold, fontSize: 11, fontWeight: '700', letterSpacing: 1, textTransform: 'uppercase' },
  monoText: { color: '#cfcdd6', fontFamily: 'Menlo', fontSize: 10.5 },
  footer: { color: color.faint, fontSize: 11, textAlign: 'center', marginTop: 16, lineHeight: 16 },
});
