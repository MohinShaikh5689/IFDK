import { StatusBar } from 'expo-status-bar';
import { useCallback, useState } from 'react';
import { Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';

import Gow3Core from './modules/gow3-core/src/Gow3CoreModule';

const POOL_MB = 256;

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
  const [status, setStatus] = useState('Milestone 2: JIT + FEXCore self-test');
  const [report, setReport] = useState(() => pretty(Gow3Core.deviceReport()));
  const [result, setResult] = useState('');
  const [log, setLog] = useState(() => Gow3Core.readLog());

  const refresh = useCallback(() => {
    setReport(pretty(Gow3Core.deviceReport()));
    setLog(Gow3Core.readLog());
  }, []);

  const enableJit = useCallback(async () => {
    setBusy(true);
    try {
      if (!Gow3Core.isDebuggerAttached() && !(await Gow3Core.isStikDebugInstalled())) {
        setStatus('StikDebug is not installed. Install it, import your pairing file and connect LocalDevVPN.');
        return;
      }
      setStatus('Waiting for StikDebug to attach…');
      const code = await Gow3Core.enableJit(POOL_MB);
      setStatus(code === 0 ? `JIT pool ready (${POOL_MB} MiB)` : `JIT pool failed: ${code}`);
    } catch (e) {
      setStatus(`JIT: ${String(e)}`);
    } finally {
      refresh();
      setBusy(false);
    }
  }, [refresh]);

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
});
