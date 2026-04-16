import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

const String NUS_SERVICE = "6e400001-b5a4-11e6-a567-006beb35965a";
const String NUS_TX      = "6e400003-b5a4-11e6-a567-006beb35965a";
const String NUS_RX      = "6e400002-b5a4-11e6-a567-006beb35965a";

const int PC_FRAME   = 0x20;
const int FB_SZ      = 64;
const int FB_PACKED  = 2048;

void main() => runApp(const PhoneCanvasApp());

class PhoneCanvasApp extends StatelessWidget {
  const PhoneCanvasApp({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'PhoneCanvas',
    debugShowCheckedModeBanner: false,
    theme: ThemeData.dark().copyWith(scaffoldBackgroundColor: Colors.black),
    home: const ScanPage(),
  );
}

// ═══════════════════════════════════════════════════════════════════════════════
// SCAN PAGE
// ═══════════════════════════════════════════════════════════════════════════════
class ScanPage extends StatefulWidget {
  const ScanPage({super.key});
  @override
  State<ScanPage> createState() => _ScanPageState();
}

class _ScanPageState extends State<ScanPage> {
  final List<ScanResult> _results = [];
  bool _scanning = false;

  void _startScan() async {
    setState(() { _results.clear(); _scanning = true; });
    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 6));
    FlutterBluePlus.scanResults.listen((results) {
      setState(() {
        for (var r in results) {
          if (!_results.any((e) => e.device.remoteId == r.device.remoteId)) {
            _results.add(r);
          }
        }
      });
    });
    await Future.delayed(const Duration(seconds: 6));
    setState(() => _scanning = false);
  }

  void _connect(BluetoothDevice device) async {
    await FlutterBluePlus.stopScan();
    await device.connect();
    if (!mounted) return;
    Navigator.pushReplacement(context, MaterialPageRoute(
      builder: (_) => CanvasPage(device: device),
    ));
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    backgroundColor: Colors.black,
    body: SafeArea(child: Column(children: [
      const SizedBox(height: 40),
      const Text('PhoneCanvas', style: TextStyle(
          color: Color(0xFF9DFF6E), fontSize: 28,
          fontWeight: FontWeight.bold, letterSpacing: 2)),
      const SizedBox(height: 8),
      const Text('connect to ESP32',
          style: TextStyle(color: Colors.grey, fontSize: 13)),
      const SizedBox(height: 32),
      ElevatedButton.icon(
        style: ElevatedButton.styleFrom(
          backgroundColor: const Color(0xFF9DFF6E),
          foregroundColor: Colors.black,
          padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 14),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
        ),
        onPressed: _scanning ? null : _startScan,
        icon: _scanning
            ? const SizedBox(width: 18, height: 18,
                child: CircularProgressIndicator(strokeWidth: 2, color: Colors.black))
            : const Icon(Icons.bluetooth_searching),
        label: Text(_scanning ? 'Scanning...' : 'Scan'),
      ),
      const SizedBox(height: 24),
      Expanded(child: ListView.builder(
        itemCount: _results.length,
        itemBuilder: (_, i) {
          final r = _results[i];
          final name = r.device.platformName.isNotEmpty
              ? r.device.platformName : 'Unknown';
          return ListTile(
            leading: const Icon(Icons.developer_board, color: Color(0xFF9DFF6E)),
            title: Text(name, style: const TextStyle(color: Colors.white)),
            subtitle: Text(r.device.remoteId.toString(),
                style: const TextStyle(color: Colors.grey, fontSize: 12)),
            trailing: Text('${r.rssi} dBm',
                style: const TextStyle(color: Color(0xFF5EAFFF), fontSize: 12)),
            onTap: () => _connect(r.device),
          );
        },
      )),
    ])),
  );
}

// ═══════════════════════════════════════════════════════════════════════════════
// CANVAS PAGE
// ═══════════════════════════════════════════════════════════════════════════════
class CanvasPage extends StatefulWidget {
  final BluetoothDevice device;
  const CanvasPage({super.key, required this.device});
  @override
  State<CanvasPage> createState() => _CanvasPageState();
}

class _CanvasPageState extends State<CanvasPage> {
  List<DrawCmd> _displayList = [];

  final List<int> _buf = [];
  bool _connected = false;
  StreamSubscription? _sub;
  BluetoothCharacteristic? _rxChar; // for ACK

  @override
  void initState() { super.initState(); _setup(); }

  Future<void> _setup() async {
    try {
      final services = await widget.device.discoverServices();
      for (var svc in services) {
        if (svc.serviceUuid.toString().toLowerCase() == NUS_SERVICE) {
          BluetoothCharacteristic? txC;
          for (var c in svc.characteristics) {
            final uuid = c.characteristicUuid.toString().toLowerCase();
            if (uuid == NUS_TX) txC = c;
            if (uuid == NUS_RX) _rxChar = c;
          }
          if (txC != null) {
            await txC.setNotifyValue(true);
            _sub = txC.onValueReceived.listen(_onBytes);
            setState(() {
              _connected = true;
              _displayList = [];
            });
            // Send initial ACK so ESP32 starts sending
            await Future.delayed(const Duration(milliseconds: 300));
            _sendAck();
          }
        }
      }
    } catch (e) { debugPrint('BLE error: $e'); }
  }

  // ── Send ACK byte to ESP32 — "ready for next frame" ──────────────────────
  void _sendAck() {
    try { _rxChar?.write([0x01], withoutResponse: true); } catch (_) {}
  }

  void _onBytes(List<int> bytes) {
    _buf.addAll(bytes);
    _drain();
  }

  void _drain() {
    // Look for PC_FRAME header byte
    while (_buf.length >= 4 + FB_PACKED) {
      // Scan for 0x20 header
      int idx = _buf.indexOf(PC_FRAME);
      if (idx < 0) { _buf.clear(); return; }
      if (idx > 0) { _buf.removeRange(0, idx); continue; }
      // Check we have full frame: 1 type + 3 length + 2048 data
      if (_buf.length < 4 + FB_PACKED) break;
      // Verify length bytes
      int len = (_buf[1] << 16) | (_buf[2] << 8) | _buf[3];
      if (len != FB_PACKED) { _buf.removeAt(0); continue; }
      // Decode 4-bit packed pixels
      final frame = <DrawCmd>[];
      for (int i = 0; i < FB_PACKED; i++) {
        int byte = _buf[4 + i];
        int p1 = (byte >> 4) & 0xF;
        int p2 = byte & 0xF;
        int px = (i * 2) % FB_SZ;
        int py = (i * 2) ~/ FB_SZ;
        int px2 = (i * 2 + 1) % FB_SZ;
        int py2 = (i * 2 + 1) ~/ FB_SZ;
        if (p1 > 0) frame.add(DrawCmd(px.toDouble(), py.toDouble(),
            Color.fromARGB(255, 0, p1 * 36, 0)));
        if (p2 > 0) frame.add(DrawCmd(px2.toDouble(), py2.toDouble(),
            Color.fromARGB(255, 0, p2 * 36, 0)));
      }
      _buf.removeRange(0, 4 + FB_PACKED);
      setState(() { _displayList = frame; });
    }
  }

  @override
  void dispose() {
    _sub?.cancel();
    _displayList = [];
    widget.device.disconnect();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    backgroundColor: Colors.black,
    body: !_connected
        ? const Center(child: Column(mainAxisSize: MainAxisSize.min, children: [
            CircularProgressIndicator(color: Color(0xFF9DFF6E)),
            SizedBox(height: 16),
            Text('Connecting...', style: TextStyle(color: Colors.grey)),
          ]))
        : Stack(children: [
            SizedBox.expand(
              child: CustomPaint(painter: CanvasPainter(_displayList)),
            ),
            Positioned(
              top: 40, left: 16,
              child: GestureDetector(
                onTap: () {
                  widget.device.disconnect();
                  Navigator.pushReplacement(context, MaterialPageRoute(
                    builder: (_) => const ScanPage()));
                },
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
                  decoration: BoxDecoration(
                    color: Colors.black.withOpacity(0.5),
                    borderRadius: BorderRadius.circular(6),
                    border: Border.all(color: Colors.grey.withOpacity(0.3)),
                  ),
                  child: const Text('← disconnect',
                      style: TextStyle(color: Colors.grey, fontSize: 11)),
                ),
              ),
            ),
          ]),
  );
}

// ═══════════════════════════════════════════════════════════════════════════════
// DRAW COMMAND — pixel dot
// ═══════════════════════════════════════════════════════════════════════════════
class DrawCmd {
  final double x, y;
  final Color color;
  const DrawCmd(this.x, this.y, this.color);
}

// ═══════════════════════════════════════════════════════════════════════════════
// CANVAS PAINTER
// ═══════════════════════════════════════════════════════════════════════════════
class CanvasPainter extends CustomPainter {
  final List<DrawCmd> cmds;
  const CanvasPainter(this.cmds);

  @override
  void paint(Canvas canvas, Size size) {
    final scale = (size.width < size.height ? size.width : size.height) / 64.0;
    final offsetX = (size.width  - 64 * scale) / 2;
    final offsetY = (size.height - 64 * scale) / 2;

    canvas.save();
    canvas.translate(offsetX, offsetY);
    canvas.scale(scale);
    canvas.drawRect(const Rect.fromLTWH(0, 0, 64, 64),
        Paint()..color = Colors.black);

    final paint = Paint()
      ..style = PaintingStyle.fill
      ..isAntiAlias = true;

    for (final cmd in cmds) {
      paint.color = cmd.color;
      canvas.drawCircle(Offset(cmd.x + 0.5, cmd.y + 0.5), 0.6, paint);
    }
    canvas.restore();
  }

  @override
  bool shouldRepaint(CanvasPainter old) => old.cmds != cmds;
}